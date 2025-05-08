<?php
// Test script to compare VALUES and FP32 vector formats

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

// Define a set of test vectors with varying dimensions
$testVectors = [
    [1.0, 2.0, 3.0, 4.0],                        // 4D vector
    [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8],    // 8D vector
    array_fill(0, 128, 0.5),                     // 128D vector (all the same value)
];

foreach ($testVectors as $index => $vector) {
    $dim = count($vector);
    echo "\n=== Testing $dim-dimensional vector ===\n";
    
    // Create unique keys for this test
    $fp32_key = "vector:fp32:$dim:" . uniqid();
    $values_key = "vector:values:$dim:" . uniqid();
    $id = "item$index";
    
    // Clean up
    $redis->del($fp32_key);
    $redis->del($values_key);

    // === Test FP32 format ===
    echo "\nTesting FP32 format\n";
    
    // Create binary data for FP32 format
    $binary_data = '';
    foreach ($vector as $val) {
        $binary_data .= pack('f', (float)$val);
    }
    
    // FP32 format
    $fp32_start = microtime(true);
    
    try {
        $result = $redis->rawCommand('VADD', $fp32_key, 'FP32', $binary_data, $id);
        $count = $redis->rawCommand('VCARD', $fp32_key);
        $fp32_time = microtime(true) - $fp32_start;
        
        echo "FP32 VADD Result: " . var_export($result, true) . "\n";
        echo "VCARD: " . var_export($count, true) . "\n";
        echo "FP32 Time: " . number_format($fp32_time * 1000, 3) . " ms\n";
    } catch (Exception $e) {
        echo "FP32 Error: " . $e->getMessage() . "\n";
    }
    
    // === Test VALUES format ===
    echo "\nTesting VALUES format\n";
    
    // VALUES format
    $values_start = microtime(true);
    
    try {
        $args = [$values_key, 'VALUES', $dim];
        foreach ($vector as $val) {
            $args[] = (string)$val;
        }
        $args[] = $id;
        
        $result = $redis->rawCommand('VADD', ...$args);
        $count = $redis->rawCommand('VCARD', $values_key);
        $values_time = microtime(true) - $values_start;
        
        echo "VALUES VADD Result: " . var_export($result, true) . "\n";
        echo "VCARD: " . var_export($count, true) . "\n";
        echo "VALUES Time: " . number_format($values_time * 1000, 3) . " ms\n";
        
        // Compare performance
        if ($fp32_time > 0 && $values_time > 0) {
            $speedup = $values_time / $fp32_time;
            echo "FP32 is " . number_format($speedup, 2) . "x faster than VALUES\n";
        }
    } catch (Exception $e) {
        echo "VALUES Error: " . $e->getMessage() . "\n";
    }
    
    // Test VSIM with both formats
    echo "\nTesting VSIM command\n";
    
    try {
        // Create a query vector with similar values
        $query_vector = array_map(function($v) {
            return $v + 0.1; // Slight modification
        }, $vector);
        
        // Query with FP32 format
        $binary_query = '';
        foreach ($query_vector as $val) {
            $binary_query .= pack('f', (float)$val);
        }
        
        $fp32_sim_start = microtime(true);
        $fp32_sim = $redis->rawCommand('VSIM', $fp32_key, 'FP32', $binary_query, 'K', '1');
        $fp32_sim_time = microtime(true) - $fp32_sim_start;
        
        echo "FP32 VSIM Result: " . json_encode($fp32_sim) . "\n";
        echo "FP32 VSIM Time: " . number_format($fp32_sim_time * 1000, 3) . " ms\n";
        
        // Query with VALUES format
        $values_args = [$values_key, 'VALUES', $dim];
        foreach ($query_vector as $val) {
            $values_args[] = (string)$val;
        }
        $values_args[] = 'K';
        $values_args[] = '1';
        
        $values_sim_start = microtime(true);
        $values_sim = $redis->rawCommand('VSIM', ...$values_args);
        $values_sim_time = microtime(true) - $values_sim_start;
        
        echo "VALUES VSIM Result: " . json_encode($values_sim) . "\n";
        echo "VALUES VSIM Time: " . number_format($values_sim_time * 1000, 3) . " ms\n";
        
        // Compare performance
        if ($fp32_sim_time > 0 && $values_sim_time > 0) {
            $sim_speedup = $values_sim_time / $fp32_sim_time;
            echo "VSIM with FP32 is " . number_format($sim_speedup, 2) . "x faster than VALUES\n";
        }
    } catch (Exception $e) {
        echo "VSIM Error: " . $e->getMessage() . "\n";
    }
    
    // Clean up
    $redis->del($fp32_key);
    $redis->del($values_key);
}

$redis->close();
echo "\nTest completed\n";