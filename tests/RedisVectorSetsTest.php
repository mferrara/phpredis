<?php defined('PHPREDIS_TESTRUN') or die('Use TestRedis.php to run tests!\n');

require_once __DIR__ . '/TestSuite.php';

class Redis_VectorSets_Test extends TestSuite {
    /**
     * @var Redis
     */
    public $redis;

    /* Sample vector data for testing */
    protected $vectors = [
        'item1' => [1.0, 2.0, 3.0, 4.0],
        'item2' => [2.0, 3.0, 4.0, 5.0],
        'item3' => [3.0, 4.0, 5.0, 6.0],
        'item4' => [4.0, 5.0, 6.0, 7.0],
        'item5' => [5.0, 6.0, 7.0, 8.0],
    ];

    /* Sample attributes for testing */
    protected $attributes = [
        'item1' => ['year' => 2020, 'category' => 'A'],
        'item2' => ['year' => 2021, 'category' => 'B'],
        'item3' => ['year' => 2022, 'category' => 'A'],
        'item4' => ['year' => 2023, 'category' => 'C'],
        'item5' => ['year' => 2024, 'category' => 'B'],
    ];

    /**
     * Helper method to skip tests if Vector Sets are not supported
     */
    protected function skipIfVectorSetsNotSupported() {
        if (!$this->minVersionCheck('8.0.0')) {
            $this->markTestSkipped('Vector Sets require Redis 8.0.0 or higher');
        }
    }

    /**
     * Test the VADD command
     */
    public function testVadd() {
        $this->skipIfVectorSetsNotSupported();

        $key = 'vectors:test';
        $this->redis->del($key);

        // Test adding a single vector
        $options = ['DIMENSIONS' => 4, 'DISTANCE_METRIC' => 'COSINE'];
        $result = $this->redis->vadd($key, 'item1', $this->vectors['item1'], $options);
        $this->assertEquals(1, $result);

        // Test adding multiple vectors
        $result = $this->redis->vadd($key, 'item2', $this->vectors['item2']);
        $this->assertEquals(1, $result);

        // Test adding a vector that already exists (should return 0)
        $result = $this->redis->vadd($key, 'item1', $this->vectors['item1']);
        $this->assertEquals(0, $result);

        // Test with invalid options
        try {
            $this->redis->vadd($key, 'item3', $this->vectors['item3'], ['INVALID_OPTION' => 'VALUE']);
            $this->fail('Should throw an exception for invalid options');
        } catch (Exception $e) {
            // Expected exception
        }

        // Clean up
        $this->redis->del($key);
    }

    /**
     * Test the VSIM command
     */
    public function testVsim() {
        $this->skipIfVectorSetsNotSupported();

        $key = 'vectors:sim:test';
        $this->redis->del($key);

        // Add test vectors
        $options = ['DIMENSIONS' => 4, 'DISTANCE_METRIC' => 'COSINE'];
        foreach ($this->vectors as $id => $vector) {
            $this->redis->vadd($key, $id, $vector, $id === 'item1' ? $options : []);
        }

        // Test basic similarity search
        $result = $this->redis->vsim($key, $this->vectors['item1'], ['K' => 3]);
        $this->assertIsArray($result);
        $this->assertCount(3, $result);

        // Test with EF parameter
        $result = $this->redis->vsim($key, $this->vectors['item2'], ['K' => 2, 'EF' => 10]);
        $this->assertIsArray($result);
        $this->assertCount(2, $result);

        // Test with FILTER option
        // First add attributes for filtering
        foreach ($this->attributes as $id => $attrs) {
            $this->redis->vsetattr($key, $id, $attrs);
        }

        $result = $this->redis->vsim($key, $this->vectors['item3'], [
            'K' => 5, 
            'FILTER' => 'category == "A"'
        ]);
        // Should only return items with category A
        $this->assertIsArray($result);

        // Clean up
        $this->redis->del($key);
    }

    /**
     * Test the VSETATTR and VGETATTR commands
     */
    public function testVectorAttributes() {
        $this->skipIfVectorSetsNotSupported();

        $key = 'vectors:attr:test';
        $this->redis->del($key);

        // Add test vectors
        $options = ['DIMENSIONS' => 4, 'DISTANCE_METRIC' => 'COSINE'];
        foreach ($this->vectors as $id => $vector) {
            $this->redis->vadd($key, $id, $vector, $id === 'item1' ? $options : []);
        }

        // Test setting attributes
        $result = $this->redis->vsetattr($key, 'item1', $this->attributes['item1']);
        $this->assertTrue($result);

        // Test getting attributes
        $attrs = $this->redis->vgetattr($key, 'item1', array_keys($this->attributes['item1']));
        $this->assertIsArray($attrs);
        $this->assertEquals($this->attributes['item1']['year'], $attrs['year']);
        $this->assertEquals($this->attributes['item1']['category'], $attrs['category']);

        // Test setting attributes on non-existent vector
        try {
            $this->redis->vsetattr($key, 'non-existent', ['test' => 'value']);
            $this->fail('Should throw an exception for non-existent vector');
        } catch (Exception $e) {
            // Expected exception
        }

        // Test getting attributes on non-existent vector
        $result = $this->redis->vgetattr($key, 'non-existent', ['test']);
        $this->assertFalse($result);

        // Test getting non-existent attributes
        $attrs = $this->redis->vgetattr($key, 'item1', ['non-existent']);
        $this->assertIsArray($attrs);
        $this->assertNull($attrs['non-existent']);

        // Clean up
        $this->redis->del($key);
    }

    /**
     * Test the VCARD and VDIM commands
     */
    public function testVcardAndVdim() {
        $this->skipIfVectorSetsNotSupported();

        $key = 'vectors:card:dim:test';
        $this->redis->del($key);

        // Add test vectors
        $options = ['DIMENSIONS' => 4, 'DISTANCE_METRIC' => 'COSINE'];
        foreach ($this->vectors as $id => $vector) {
            $this->redis->vadd($key, $id, $vector, $id === 'item1' ? $options : []);
        }

        // Test VCARD
        $result = $this->redis->vcard($key);
        $this->assertEquals(count($this->vectors), $result);

        // Test VDIM
        $result = $this->redis->vdim($key);
        $this->assertEquals(4, $result);

        // Test on non-existent key
        $result = $this->redis->vcard('non-existent');
        $this->assertEquals(0, $result);

        $result = $this->redis->vdim('non-existent');
        $this->assertFalse($result);

        // Clean up
        $this->redis->del($key);
    }
}