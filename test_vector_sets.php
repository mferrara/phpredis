<?php
// Test script for Vector Sets implementation in phpredis

// Check if Redis extension is loaded
if (!extension_loaded('redis')) {
    die("Redis extension not loaded\n");
}

// Output extension details
$redis_version = phpversion('redis');
$reflection = new ReflectionExtension('redis');

echo "Redis extension loaded successfully. Version: $redis_version\n";
echo "Testing Vector Sets commands...\n\n";

// Print all available methods to verify our implementation
echo "Available Redis methods:\n";
$redis_class = new ReflectionClass('Redis');
$has_vadd = $redis_class->hasMethod('vadd');
$has_vsim = $redis_class->hasMethod('vsim');
$has_vcard = $redis_class->hasMethod('vcard');
$has_vdim = $redis_class->hasMethod('vdim');

echo "- VADD method exists: " . ($has_vadd ? "YES" : "NO") . "\n";
echo "- VSIM method exists: " . ($has_vsim ? "YES" : "NO") . "\n";
echo "- VCARD method exists: " . ($has_vcard ? "YES" : "NO") . "\n";
echo "- VDIM method exists: " . ($has_vdim ? "YES" : "NO") . "\n\n";

// Redis connection settings for Docker container with Redis 8
// In Docker networking, we can use either the container IP, container name, or host.docker.internal
// Try all possible connection methods
$redis_container = getenv('REDIS_HOST') ?: 'redis-vector';

$connectionOptions = [
    // Option 1: Try container name from environment variable or default to redis-vector
    ['host' => $redis_container, 'port' => 6379], 
    // Option 2: Use Docker host network IP
    ['host' => 'host.docker.internal', 'port' => 6379],
    // Option 3: Try redis-vector name directly
    ['host' => 'redis-vector', 'port' => 6379],
    // Option 4: Try default Docker network gateway
    ['host' => '172.17.0.1', 'port' => 6379]
];

$redisConnected = false;
$redisUsername = 'default';
$redisPassword = 'redispw';
$redisDb = 0;

// Connect to Redis server
$redis = new Redis();
$lastError = "";

// Try all connection options until one works
foreach ($connectionOptions as $option) {
    try {
        echo "Trying to connect to Redis at {$option['host']}:{$option['port']}...\n";
        $redis->connect($option['host'], $option['port'], 3, NULL, 200); // 3 sec timeout, 200ms retry
        
        // Auth if credentials are provided
        if (!empty($redisPassword)) {
            try {
                if (!empty($redisUsername)) {
                    $redis->auth([$redisUsername, $redisPassword]);
                } else {
                    $redis->auth($redisPassword);
                }
            } catch (Exception $authEx) {
                echo "Auth failed, trying without auth...\n";
                // Continue without auth
            }
        }
        
        // Select database
        $redis->select($redisDb);
        
        // Test connection
        $pong = $redis->ping();
        if ($pong) {
            $redisConnected = true;
            echo "Connected to Redis server at {$option['host']}:{$option['port']} (db: $redisDb)\n";
            break;
        }
    } catch (Exception $e) {
        $lastError = $e->getMessage();
        echo "Failed to connect to Redis at {$option['host']}:{$option['port']}: " . $lastError . "\n";
        continue;
    }
}

if (!$redisConnected) {
    die("Failed to connect to any Redis server. Last error: " . $lastError . "\n");
}

// Check Redis server version
$info = $redis->info();
echo "Redis server version: " . $info['redis_version'] . "\n";

// Check if Redis 8.0.0 or higher is available
if (version_compare($info['redis_version'], '8.0.0', '<')) {
    echo "WARNING: Vector Sets require Redis 8.0.0 or higher for actual functionality.\n";
    echo "You have Redis version {$info['redis_version']}.\n";
    echo "We'll continue testing the command interface to verify our extension implementation.\n";
    echo "Commands will likely fail with ERR Unknown commands, which is expected on Redis < 8.0.0.\n";
} else {
    echo "Redis server version {$info['redis_version']} supports Vector Sets.\n";
}

// Function to run a test with error handling
function runTest($redis, $testName, $callback) {
    echo "\nTesting $testName...\n";
    try {
        $result = $callback($redis);
        echo "$testName Result: " . print_r($result, true) . "\n";
        return $result;
    } catch (Exception $e) {
        echo "$testName Error: " . $e->getMessage() . "\n";
        return false;
    }
}

// Test if a method exists in the Redis class
function methodExists($methodName) {
    $redis_class = new ReflectionClass('Redis');
    return $redis_class->hasMethod($methodName);
}

// Set a unique test key
$testKey = 'vector:test:' . uniqid();

// Clean up any existing keys
$redis->del($testKey);

// Define test vector and ID
$vector = [1.0, 2.0, 3.0, 4.0];
$id = 'item1';

// Test VADD Command if it exists
if (methodExists('vadd')) {
    runTest($redis, 'VADD', function($redis) use ($testKey, $id, $vector) {
        // Add vector with options
        $options = ['DIMENSIONS' => 4, 'DISTANCE_METRIC' => 'COSINE'];
        return $redis->vadd($testKey, $id, $vector, $options);
    });
} else {
    echo "\nVADD method not available in this Redis extension.\n";
}

// Test VCARD Command if it exists
if (methodExists('vcard')) {
    runTest($redis, 'VCARD', function($redis) use ($testKey) {
        return $redis->vcard($testKey);
    });
} else {
    echo "\nVCARD method not available in this Redis extension.\n";
}

// Test VDIM Command if it exists
if (methodExists('vdim')) {
    runTest($redis, 'VDIM', function($redis) use ($testKey) {
        return $redis->vdim($testKey);
    });
} else {
    echo "\nVDIM method not available in this Redis extension.\n";
}

// Test VSIM Command if it exists
if (methodExists('vsim')) {
    runTest($redis, 'VSIM', function($redis) use ($testKey, $vector) {
        $options = ['K' => 3];
        return $redis->vsim($testKey, $vector, $options);
    });
} else {
    echo "\nVSIM method not available in this Redis extension.\n";
}

// Clean up
$redis->del($testKey);
$redis->close();

echo "\nTest completed.\n";