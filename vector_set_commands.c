/* Implementation of Redis Vector Set Commands for phpredis */

#include "php_redis.h"
#include "common.h"
#include "library.h"
#include <zend_exceptions.h>

/* Constants for vector formats */
#define VECTOR_FORMAT_VALUES 1
#define VECTOR_FORMAT_FP32   2

/* Configuration option for default vector format (can be changed at runtime) */
static int vector_default_format = VECTOR_FORMAT_FP32;

/* Implementation of VCARD command */
int redis_vcard_cmd(INTERNAL_FUNCTION_PARAMETERS, RedisSock *redis_sock,
                   char *kw, char **cmd, int *cmd_len, short *slot,
                   void **ctx)
{
    char *key;
    size_t key_len;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(key, key_len)
    ZEND_PARSE_PARAMETERS_END_EX(return FAILURE);
    
    // Command is simply VCARD key
    *cmd_len = REDIS_CMD_SPPRINTF(cmd, kw, "s", key, key_len);
    
    // Set slot if in cluster mode
    if (slot) *slot = cluster_hash_key(key, key_len);
    
    return SUCCESS;
}

/* Implementation of VDIM command */
int redis_vdim_cmd(INTERNAL_FUNCTION_PARAMETERS, RedisSock *redis_sock,
                  char *kw, char **cmd, int *cmd_len, short *slot,
                  void **ctx)
{
    char *key;
    size_t key_len;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(key, key_len)
    ZEND_PARSE_PARAMETERS_END_EX(return FAILURE);
    
    // Command is simply VDIM key
    *cmd_len = REDIS_CMD_SPPRINTF(cmd, kw, "s", key, key_len);
    
    // Set slot if in cluster mode
    if (slot) *slot = cluster_hash_key(key, key_len);
    
    return SUCCESS;
}

/* Helper function to serialize a vector as VALUES format */
static int serialize_vector_values(zval *z_vector, smart_string *cmdstr) {
    zval *z_val;
    HashTable *ht = Z_ARRVAL_P(z_vector);
    int vector_size = zend_hash_num_elements(ht);
    
    /* Add VALUES keyword */
    redis_cmd_append_sstr(cmdstr, "VALUES", sizeof("VALUES") - 1);
    
    /* Add vector dimension */
    redis_cmd_append_sstr_long(cmdstr, vector_size);
    
    /* Add each value as string */
    ZEND_HASH_FOREACH_VAL(ht, z_val) {
        switch (Z_TYPE_P(z_val)) {
            case IS_LONG:
                redis_cmd_append_sstr_long(cmdstr, Z_LVAL_P(z_val));
                break;
            case IS_DOUBLE:
                redis_cmd_append_sstr_dbl(cmdstr, Z_DVAL_P(z_val));
                break;
            default:
                zend_string *val_str = zval_get_string(z_val);
                redis_cmd_append_sstr(cmdstr, ZSTR_VAL(val_str), ZSTR_LEN(val_str));
                zend_string_release(val_str);
                break;
        }
    } ZEND_HASH_FOREACH_END();
    
    return SUCCESS;
}

/* Helper function to serialize a vector as FP32 binary format */
static int serialize_vector_fp32(zval *z_vector, smart_string *cmdstr) {
    zval *z_val;
    HashTable *ht = Z_ARRVAL_P(z_vector);
    int vector_size = zend_hash_num_elements(ht);
    
    /* Create a buffer to hold binary data */
    float *fp32_data = emalloc(vector_size * sizeof(float));
    if (!fp32_data) {
        return FAILURE;
    }
    
    /* Convert all values to float and store in binary format */
    int i = 0;
    ZEND_HASH_FOREACH_VAL(ht, z_val) {
        if (i >= vector_size) break;
        
        if (Z_TYPE_P(z_val) == IS_LONG) {
            fp32_data[i] = (float)Z_LVAL_P(z_val);
        } else if (Z_TYPE_P(z_val) == IS_DOUBLE) {
            fp32_data[i] = (float)Z_DVAL_P(z_val);
        } else {
            /* Try to convert to float */
            zval z_copy;
            ZVAL_COPY(&z_copy, z_val);
            convert_to_double(&z_copy);
            fp32_data[i] = (float)Z_DVAL(z_copy);
            zval_ptr_dtor(&z_copy);
        }
        i++;
    } ZEND_HASH_FOREACH_END();
    
    /* Add FP32 keyword */
    redis_cmd_append_sstr(cmdstr, "FP32", sizeof("FP32") - 1);
    
    /* Add binary data */
    redis_cmd_append_sstr(cmdstr, (char*)fp32_data, vector_size * sizeof(float));
    
    /* Free the buffer */
    efree(fp32_data);
    
    return SUCCESS;
}

/* Implementation of VADD command */
int redis_vadd_cmd(INTERNAL_FUNCTION_PARAMETERS, RedisSock *redis_sock,
                    char *kw, char **cmd, int *cmd_len, short *slot,
                    void **ctx)
{
    char *key;
    size_t key_len;
    zval *z_id, *z_vector, *z_options = NULL;
    HashTable *ht_options = NULL;
    zend_string *id_str = NULL;
    smart_string cmdstr = {0};
    int vector_format = vector_default_format; /* Default format is FP32 */
    
    ZEND_PARSE_PARAMETERS_START(3, 4)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_ZVAL(z_id)
        Z_PARAM_ZVAL(z_vector)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_HT_OR_NULL(ht_options)
    ZEND_PARSE_PARAMETERS_END_EX(return FAILURE);
    
    /* Verify vector is an array */
    if (Z_TYPE_P(z_vector) != IS_ARRAY) {
        php_error_docref(NULL, E_WARNING, "Vector must be an array of numeric values");
        return FAILURE;
    }
    
    /* Check if vector format is specified in options */
    if (ht_options != NULL) {
        zval *format_zv = zend_hash_str_find(ht_options, "format", sizeof("format") - 1);
        if (format_zv != NULL) {
            if (Z_TYPE_P(format_zv) == IS_LONG) {
                vector_format = Z_LVAL_P(format_zv);
            } else if (Z_TYPE_P(format_zv) == IS_STRING) {
                if (strcasecmp(Z_STRVAL_P(format_zv), "values") == 0) {
                    vector_format = VECTOR_FORMAT_VALUES;
                } else if (strcasecmp(Z_STRVAL_P(format_zv), "fp32") == 0) {
                    vector_format = VECTOR_FORMAT_FP32;
                }
            }
            
            /* Remove format from options so it's not sent to Redis */
            zend_hash_str_del(ht_options, "format", sizeof("format") - 1);
        }
    }
    
    /* Initialize command string */
    redis_cmd_init_sstr(&cmdstr, 4 + (ht_options ? zend_hash_num_elements(ht_options) * 2 : 0), kw, strlen(kw));
    
    /* Add key */
    redis_cmd_append_sstr_key(&cmdstr, key, key_len, redis_sock, slot);
    
    /* Serialize vector according to format */
    if (vector_format == VECTOR_FORMAT_VALUES) {
        if (serialize_vector_values(z_vector, &cmdstr) == FAILURE) {
            if (id_str) zend_string_release(id_str);
            smart_string_free(&cmdstr);
            return FAILURE;
        }
    } else {
        /* Default to FP32 */
        if (serialize_vector_fp32(z_vector, &cmdstr) == FAILURE) {
            if (id_str) zend_string_release(id_str);
            smart_string_free(&cmdstr);
            return FAILURE;
        }
    }
    
    /* Add id - convert to string if needed */
    switch (Z_TYPE_P(z_id)) {
        case IS_STRING:
            redis_cmd_append_sstr(&cmdstr, Z_STRVAL_P(z_id), Z_STRLEN_P(z_id));
            break;
        case IS_LONG:
            redis_cmd_append_sstr_long(&cmdstr, Z_LVAL_P(z_id));
            break;
        case IS_DOUBLE:
            redis_cmd_append_sstr_dbl(&cmdstr, Z_DVAL_P(z_id));
            break;
        default:
            /* Create a copy to convert to string */
            id_str = zval_get_string(z_id);
            redis_cmd_append_sstr(&cmdstr, ZSTR_VAL(id_str), ZSTR_LEN(id_str));
            break;
    }
    
    /* Add options if provided */
    if (ht_options) {
        zend_string *key;
        zval *z_val;
        
        ZEND_HASH_FOREACH_STR_KEY_VAL(ht_options, key, z_val) {
            if (key == NULL) continue;
            
            /* Add option name (uppercase) */
            char *upper_key = estrndup(ZSTR_VAL(key), ZSTR_LEN(key));
            int i;
            for (i = 0; i < ZSTR_LEN(key); i++) {
                upper_key[i] = toupper(upper_key[i]);
            }
            redis_cmd_append_sstr(&cmdstr, upper_key, ZSTR_LEN(key));
            efree(upper_key);
            
            /* Add option value */
            switch (Z_TYPE_P(z_val)) {
                case IS_STRING:
                    redis_cmd_append_sstr(&cmdstr, Z_STRVAL_P(z_val), Z_STRLEN_P(z_val));
                    break;
                case IS_LONG:
                    redis_cmd_append_sstr_long(&cmdstr, Z_LVAL_P(z_val));
                    break;
                case IS_DOUBLE:
                    redis_cmd_append_sstr_dbl(&cmdstr, Z_DVAL_P(z_val));
                    break;
                default:
                    zend_string *val_str = zval_get_string(z_val);
                    redis_cmd_append_sstr(&cmdstr, ZSTR_VAL(val_str), ZSTR_LEN(val_str));
                    zend_string_release(val_str);
                    break;
            }
        } ZEND_HASH_FOREACH_END();
    }
    
    /* Clean up resources */
    if (id_str) zend_string_release(id_str);
    
    /* Return the command */
    *cmd = cmdstr.c;
    *cmd_len = cmdstr.len;

    return SUCCESS;
}

/* Implementation of VSIM command */
int redis_vsim_cmd(INTERNAL_FUNCTION_PARAMETERS, RedisSock *redis_sock,
                   char *kw, char **cmd, int *cmd_len, short *slot,
                   void **ctx)
{
    char *key;
    size_t key_len;
    zval *z_vector, *z_options = NULL;
    HashTable *ht_options = NULL;
    smart_string cmdstr = {0};
    int vector_format = vector_default_format; /* Default format is FP32 */
    
    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_ZVAL(z_vector)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_HT_OR_NULL(ht_options)
    ZEND_PARSE_PARAMETERS_END_EX(return FAILURE);
    
    /* Verify vector is an array */
    if (Z_TYPE_P(z_vector) != IS_ARRAY) {
        php_error_docref(NULL, E_WARNING, "Vector must be an array of numeric values");
        return FAILURE;
    }
    
    /* Check if vector format is specified in options */
    if (ht_options != NULL) {
        zval *format_zv = zend_hash_str_find(ht_options, "format", sizeof("format") - 1);
        if (format_zv != NULL) {
            if (Z_TYPE_P(format_zv) == IS_LONG) {
                vector_format = Z_LVAL_P(format_zv);
            } else if (Z_TYPE_P(format_zv) == IS_STRING) {
                if (strcasecmp(Z_STRVAL_P(format_zv), "values") == 0) {
                    vector_format = VECTOR_FORMAT_VALUES;
                } else if (strcasecmp(Z_STRVAL_P(format_zv), "fp32") == 0) {
                    vector_format = VECTOR_FORMAT_FP32;
                }
            }
            
            /* Remove format from options so it's not sent to Redis */
            zend_hash_str_del(ht_options, "format", sizeof("format") - 1);
        }
    }
    
    /* Initialize command string */
    redis_cmd_init_sstr(&cmdstr, 3 + (ht_options ? zend_hash_num_elements(ht_options) * 2 : 0), kw, strlen(kw));
    
    /* Add key */
    redis_cmd_append_sstr_key(&cmdstr, key, key_len, redis_sock, slot);
    
    /* Serialize vector according to format */
    if (vector_format == VECTOR_FORMAT_VALUES) {
        if (serialize_vector_values(z_vector, &cmdstr) == FAILURE) {
            smart_string_free(&cmdstr);
            return FAILURE;
        }
    } else {
        /* Default to FP32 */
        if (serialize_vector_fp32(z_vector, &cmdstr) == FAILURE) {
            smart_string_free(&cmdstr);
            return FAILURE;
        }
    }
    
    /* Add options if provided */
    if (ht_options) {
        zend_string *key;
        zval *z_val;
        
        ZEND_HASH_FOREACH_STR_KEY_VAL(ht_options, key, z_val) {
            if (key == NULL) continue;
            
            /* Add option name (uppercase) */
            char *upper_key = estrndup(ZSTR_VAL(key), ZSTR_LEN(key));
            int i;
            for (i = 0; i < ZSTR_LEN(key); i++) {
                upper_key[i] = toupper(upper_key[i]);
            }
            redis_cmd_append_sstr(&cmdstr, upper_key, ZSTR_LEN(key));
            efree(upper_key);
            
            /* Add option value */
            switch (Z_TYPE_P(z_val)) {
                case IS_STRING:
                    redis_cmd_append_sstr(&cmdstr, Z_STRVAL_P(z_val), Z_STRLEN_P(z_val));
                    break;
                case IS_LONG:
                    redis_cmd_append_sstr_long(&cmdstr, Z_LVAL_P(z_val));
                    break;
                case IS_DOUBLE:
                    redis_cmd_append_sstr_dbl(&cmdstr, Z_DVAL_P(z_val));
                    break;
                default:
                    zend_string *val_str = zval_get_string(z_val);
                    redis_cmd_append_sstr(&cmdstr, ZSTR_VAL(val_str), ZSTR_LEN(val_str));
                    zend_string_release(val_str);
                    break;
            }
        } ZEND_HASH_FOREACH_END();
    }
    
    /* Return the command */
    *cmd = cmdstr.c;
    *cmd_len = cmdstr.len;

    return SUCCESS;
}

/* Implementation of VSETATTR command */
int redis_vsetattr_cmd(INTERNAL_FUNCTION_PARAMETERS, RedisSock *redis_sock,
                       char *kw, char **cmd, int *cmd_len, short *slot,
                       void **ctx)
{
    char *key, *id, *json_str;
    size_t key_len, id_len, json_len;
    smart_string cmdstr = {0};
    
    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_STRING(id, id_len)
        Z_PARAM_STRING(json_str, json_len)
    ZEND_PARSE_PARAMETERS_END_EX(return FAILURE);
    
    /* Initialize command string */
    redis_cmd_init_sstr(&cmdstr, 3, kw, strlen(kw));
    
    /* Add key and ID */
    redis_cmd_append_sstr_key(&cmdstr, key, key_len, redis_sock, slot);
    redis_cmd_append_sstr(&cmdstr, id, id_len);
    
    /* Add JSON object as a string */
    redis_cmd_append_sstr(&cmdstr, json_str, json_len);
    
    /* Return the command */
    *cmd = cmdstr.c;
    *cmd_len = cmdstr.len;

    return SUCCESS;
}

/* Implementation of VGETATTR command */
int redis_vgetattr_cmd(INTERNAL_FUNCTION_PARAMETERS, RedisSock *redis_sock,
                      char *kw, char **cmd, int *cmd_len, short *slot,
                      void **ctx)
{
    char *key, *id;
    size_t key_len, id_len;
    smart_string cmdstr = {0};
    
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_STRING(id, id_len)
    ZEND_PARSE_PARAMETERS_END_EX(return FAILURE);
    
    /* Initialize command string */
    redis_cmd_init_sstr(&cmdstr, 2, kw, strlen(kw));
    
    /* Add key and ID */
    redis_cmd_append_sstr_key(&cmdstr, key, key_len, redis_sock, slot);
    redis_cmd_append_sstr(&cmdstr, id, id_len);
    
    /* Return the command */
    *cmd = cmdstr.c;
    *cmd_len = cmdstr.len;

    return SUCCESS;
}