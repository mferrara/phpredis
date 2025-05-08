<?php
// Test Vector Sets with binary data format

// Check if Redis extension is loaded
if (!extension_loaded('redis')) {
    die("Redis extension not loaded\n");
}

// Connect to Redis
$redis = new Redis();
$redis_host = getenv('REDIS_HOST') ?: 'redis-vector';

try {
    echo "Connecting to Redis at $redis_host:6379...\n";
    $redis->connect($redis_host, 6379, 3);
    echo "Connected successfully\n";
} catch (Exception $e) {
    try {
        echo "Retrying with redis-vector...\n";
        $redis->connect('redis-vector', 6379, 3);
        echo "Connected successfully\n";
    } catch (Exception $e) {
        die("Failed to connect to Redis: " . $e->getMessage() . "\n");
    }
}

// Get Redis info
$info = $redis->info();
echo "Redis version: " . $info['redis_version'] . "\n";

// Test the FP32 format for vector data
echo "\nTesting FP32 format for vectors...\n";

// Define a test vector
$vector = [1.0, 2.0, 3.0, 4.0];
$dimension = count($vector);
$id = "fp32-test";
$fp32_key = "vector:fp32:" . uniqid();

// Clear the key
$redis->del($fp32_key);

// Create binary representation of the vector as FP32 (32-bit single precision floats)
$binary_data = '';
foreach ($vector as $val) {
    $binary_data .= pack('f', (float)$val);  // 'f' format for single precision float (FP32)
}

// Debug info
echo "Vector: " . json_encode($vector) . "\n";
echo "Dimension: $dimension\n";
echo "Binary size: " . strlen($binary_data) . " bytes\n";
echo "Bytes per float: " . (strlen($binary_data) / count($vector)) . "\n";

// Try with FP32 format
try {
    echo "\nExecuting raw VADD with FP32 format...\n";
    $result = $redis->rawCommand('VADD', $fp32_key, 'FP32', $binary_data, $id);
    echo "Raw FP32 VADD Result: " . var_export($result, true) . "\n";
    
    // Check if it worked
    $count = $redis->rawCommand('VCARD', $fp32_key);
    echo "Raw VCARD Result: " . var_export($count, true) . "\n";
    
    if ($count > 0) {
        echo "Successfully added vector using FP32 format with raw command!\n";
    } else {
        echo "Failed to add vector using FP32 format with raw command.\n";
    }
} catch (Exception $e) {
    echo "Raw FP32 VADD Error: " . $e->getMessage() . "\n";
}

// Try with our PHPRedis implementation
$test_key = "vector:phpredis:" . uniqid();
$redis->del($test_key);

try {
    echo "\nExecuting PHPRedis VADD (should use FP32 by default)...\n";
    // FP32 is the default, no need to specify format
    $result = $redis->vadd($test_key, $id, $vector);
    echo "PHPRedis VADD Result: " . var_export($result, true) . "\n";
    
    // Check if it worked
    $count = $redis->vcard($test_key);
    echo "PHPRedis VCARD Result: " . var_export($count, true) . "\n";
    
    if ($count > 0) {
        echo "Successfully added vector using default FP32 format with PHPRedis!\n";
    } else {
        echo "Failed to add vector using default FP32 format with PHPRedis.\n";
    }
} catch (Exception $e) {
    echo "PHPRedis VADD Error: " . $e->getMessage() . "\n";
}

// Try with explicit VALUES format
$values_key = "vector:values:" . uniqid();
$redis->del($values_key);

try {
    echo "\nExecuting PHPRedis VADD with VALUES format...\n";
    $options = ['format' => 'VALUES'];
    $result = $redis->vadd($values_key, $id, $vector, $options);
    echo "PHPRedis VALUES VADD Result: " . var_export($result, true) . "\n";
    
    // Check if it worked
    $count = $redis->vcard($values_key);
    echo "PHPRedis VALUES VCARD Result: " . var_export($count, true) . "\n";
    
    if ($count > 0) {
        echo "Successfully added vector using VALUES format with PHPRedis!\n";
    } else {
        echo "Failed to add vector using VALUES format with PHPRedis.\n";
    }
} catch (Exception $e) {
    echo "PHPRedis VALUES VADD Error: " . $e->getMessage() . "\n";
}

// Try VSIM
$query_vector = [1.1, 2.2, 3.1, 4.2]; // Similar but not identical
try {
    echo "\nTesting VSIM with FP32 format...\n";
    $sim_result = $redis->vsim($fp32_key, $query_vector);
    echo "PHPRedis VSIM Result: " . print_r($sim_result, true) . "\n";
} catch (Exception $e) {
    echo "PHPRedis VSIM Error: " . $e->getMessage() . "\n";
}

// Clean up
$redis->del($fp32_key);
$redis->del($test_key);
$redis->del($values_key);
$redis->close();

echo "\nTest completed\n";