/* Implementation of Redis Vector Set Commands for phpredis */

#include "php_redis.h"
#include "common.h"
#include "library.h"
#include <zend_exceptions.h>

/* Helper function to serialize a PHP array to a Redis vector */
static int serialize_vector(RedisSock *redis_sock, zval *z_vector, smart_string *vector_str) {
    zval *z_val;
    zend_string *tmp_str;
    HashTable *ht;

    /* Make sure we have an array */
    if (Z_TYPE_P(z_vector) != IS_ARRAY) {
        php_error_docref(NULL, E_WARNING, "Vector must be an array of floats or binary data");
        return FAILURE;
    }

    ht = Z_ARRVAL_P(z_vector);
    
    /* Iterate through vector elements */
    ZEND_HASH_FOREACH_VAL(ht, z_val) {
        /* For now, we only support float values in vectors */
        if (Z_TYPE_P(z_val) == IS_LONG) {
            /* Convert integer to float */
            double dval = (double)Z_LVAL_P(z_val);
            smart_string_appendl(vector_str, (char*)&dval, sizeof(double));
        } else if (Z_TYPE_P(z_val) == IS_DOUBLE) {
            /* Append float value directly */
            smart_string_appendl(vector_str, (char*)&Z_DVAL_P(z_val), sizeof(double));
        } else {
            /* Try to convert to string */
            zval z_copy;
            ZVAL_COPY(&z_copy, z_val);
            convert_to_string(&z_copy);
            tmp_str = Z_STR(z_copy);
            
            smart_string_appendl(vector_str, ZSTR_VAL(tmp_str), ZSTR_LEN(tmp_str));
            zval_ptr_dtor(&z_copy);
        }
    } ZEND_HASH_FOREACH_END();

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
    
    ZEND_PARSE_PARAMETERS_START(3, 4)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_ZVAL(z_id)
        Z_PARAM_ZVAL(z_vector)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_HT_OR_NULL(ht_options)
    ZEND_PARSE_PARAMETERS_END_EX(return FAILURE);
    
    /* Initialize command string */
    redis_cmd_init_sstr(&cmdstr, 3 + (ht_options ? zend_hash_num_elements(ht_options) * 2 : 0), kw, strlen(kw));
    
    /* Add key */
    redis_cmd_append_sstr_key(&cmdstr, key, key_len, redis_sock, slot);
    
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
    
    /* Convert vector array to serialized format */
    smart_string vector_str = {0};
    if (serialize_vector(redis_sock, z_vector, &vector_str) == FAILURE) {
        if (id_str) zend_string_release(id_str);
        smart_string_free(&cmdstr);
        return FAILURE;
    }
    
    /* Add vector data */
    redis_cmd_append_sstr(&cmdstr, vector_str.c, vector_str.len);
    smart_string_free(&vector_str);
    
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
    
    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_ZVAL(z_vector)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_HT_OR_NULL(ht_options)
    ZEND_PARSE_PARAMETERS_END_EX(return FAILURE);
    
    /* Initialize command string */
    redis_cmd_init_sstr(&cmdstr, 2 + (ht_options ? zend_hash_num_elements(ht_options) * 2 : 0), kw, strlen(kw));
    
    /* Add key */
    redis_cmd_append_sstr_key(&cmdstr, key, key_len, redis_sock, slot);
    
    /* Convert vector array to serialized format */
    smart_string vector_str = {0};
    if (serialize_vector(redis_sock, z_vector, &vector_str) == FAILURE) {
        smart_string_free(&cmdstr);
        return FAILURE;
    }
    
    /* Add vector data */
    redis_cmd_append_sstr(&cmdstr, vector_str.c, vector_str.len);
    smart_string_free(&vector_str);
    
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
    char *key, *id;
    size_t key_len, id_len;
    zval *z_attrs;
    HashTable *ht_attrs;
    smart_string cmdstr = {0};
    
    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_STRING(id, id_len)
        Z_PARAM_ARRAY_HT(ht_attrs)
    ZEND_PARSE_PARAMETERS_END_EX(return FAILURE);
    
    /* Initialize command string */
    redis_cmd_init_sstr(&cmdstr, 2 + (zend_hash_num_elements(ht_attrs) * 2), kw, strlen(kw));
    
    /* Add key and ID */
    redis_cmd_append_sstr_key(&cmdstr, key, key_len, redis_sock, slot);
    redis_cmd_append_sstr(&cmdstr, id, id_len);
    
    /* Add attributes */
    zend_string *attr_key;
    zval *z_val;
    
    ZEND_HASH_FOREACH_STR_KEY_VAL(ht_attrs, attr_key, z_val) {
        if (attr_key == NULL) continue;
        
        /* Add attribute name */
        redis_cmd_append_sstr(&cmdstr, ZSTR_VAL(attr_key), ZSTR_LEN(attr_key));
        
        /* Add attribute value */
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
    zval *z_attrs;
    HashTable *ht_attrs;
    smart_string cmdstr = {0};
    
    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_STRING(id, id_len)
        Z_PARAM_ARRAY_HT(ht_attrs)
    ZEND_PARSE_PARAMETERS_END_EX(return FAILURE);
    
    /* Initialize command string */
    redis_cmd_init_sstr(&cmdstr, 2 + zend_hash_num_elements(ht_attrs), kw, strlen(kw));
    
    /* Add key and ID */
    redis_cmd_append_sstr_key(&cmdstr, key, key_len, redis_sock, slot);
    redis_cmd_append_sstr(&cmdstr, id, id_len);
    
    /* Add attribute names */
    zval *z_attr;
    ZEND_HASH_FOREACH_VAL(ht_attrs, z_attr) {
        zend_string *attr_str = zval_get_string(z_attr);
        redis_cmd_append_sstr(&cmdstr, ZSTR_VAL(attr_str), ZSTR_LEN(attr_str));
        zend_string_release(attr_str);
    } ZEND_HASH_FOREACH_END();
    
    /* Return the command */
    *cmd = cmdstr.c;
    *cmd_len = cmdstr.len;
    
    return SUCCESS;
}