<?php
// Test proper VADD command format

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

// Set test key
$testKey = 'vector:test:' . uniqid();
echo "Using test key: $testKey\n";

// Clean existing keys
$redis->del($testKey);

// Test vector data
$vector = [0.1, 1.2, 0.5];
$id = "my-element";

// Try using our phpredis implementation with explicit VALUES format
echo "\nTrying our VADD implementation with VALUES format...\n";
try {
    $options = ['format' => 'VALUES'];
    $result = $redis->vadd($testKey, $id, $vector, $options);
    echo "VADD Result: " . var_export($result, true) . "\n";
} catch (Exception $e) {
    echo "VADD Error: " . $e->getMessage() . "\n";
}

// Check if it worked
try {
    $count = $redis->vcard($testKey);
    echo "VCARD Result: " . var_export($count, true) . "\n";
} catch (Exception $e) {
    echo "VCARD Error: " . $e->getMessage() . "\n";
}

// Test with known working format directly
echo "\nTesting with direct VALUES format: VADD key VALUES dimension value1 value2 value3 id\n";
$rawKey = 'vector:raw:' . uniqid(); 
$redis->del($rawKey);

// Format: VADD mykey VALUES 3 0.1 1.2 0.5 my-element
// This is the exact format that worked in previous tests
try {
    $args = [$rawKey, 'VALUES', count($vector)]; 
    foreach ($vector as $val) {
        $args[] = (string)$val;
    }
    $args[] = $id;

    echo "Command: VADD " . implode(' ', $args) . "\n";
    $result = $redis->rawCommand('VADD', ...$args);
    echo "Raw VADD Result: " . var_export($result, true) . "\n";

    // Check if it worked
    $raw_count = $redis->rawCommand('VCARD', $rawKey);  
    echo "Raw VCARD Result: " . var_export($raw_count, true) . "\n";
} catch (Exception $e) {
    echo "Raw VADD Error: " . $e->getMessage() . "\n";
}

// Try VSIM with VALUES format
$query_vector = [0.2, 1.1, 0.6]; // Similar but not identical
try {
    echo "\nTesting VSIM with VALUES format...\n";
    $options = ['format' => 'VALUES'];
    $sim_result = $redis->vsim($testKey, $query_vector, $options);
    echo "VSIM Result with VALUES format: " . print_r($sim_result, true) . "\n";
} catch (Exception $e) {
    echo "VSIM Error: " . $e->getMessage() . "\n";
}

// Clean up
$redis->del($testKey);
$redis->del($rawKey);
$redis->close();

echo "\nTest completed\n";