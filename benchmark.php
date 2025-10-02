#!/usr/bin/env php
<?php
/**
 * Benchmark phan_unique_types() vs PHP implementation
 */

// Check if extension is loaded
if (!function_exists('phan_unique_types')) {
    die("ERROR: phan_helpers extension not loaded\n");
}

// PHP implementation (from UnionType.php)
function php_unique_types(array $type_list): array
{
    $new_type_list = [];
    if (\count($type_list) >= 8) {
        foreach ($type_list as $type) {
            $new_type_list[\spl_object_id($type)] = $type;
        }
        return \array_values($new_type_list);
    }
    foreach ($type_list as $type) {
        if (!\in_array($type, $new_type_list, true)) {
            $new_type_list[] = $type;
        }
    }
    return $new_type_list;
}

class TestType {
    public $id;
    public function __construct($id) {
        $this->id = $id;
    }
}

function benchmark($size, $duplicate_factor, $iterations) {
    // Create unique objects
    $unique_objects = [];
    for ($i = 0; $i < $size; $i++) {
        $unique_objects[] = new TestType($i);
    }

    // Create test array with duplicates
    $test_array = [];
    for ($i = 0; $i < $duplicate_factor; $i++) {
        foreach ($unique_objects as $obj) {
            $test_array[] = $obj;
        }
    }
    shuffle($test_array);

    echo str_repeat("=", 70) . "\n";
    echo "Array size: " . count($test_array) . " (unique: $size, duplication factor: {$duplicate_factor}x)\n";
    echo "Iterations: $iterations\n";
    echo str_repeat("-", 70) . "\n";

    // Warmup
    php_unique_types($test_array);
    phan_unique_types($test_array);

    // Benchmark PHP implementation
    $start = microtime(true);
    for ($i = 0; $i < $iterations; $i++) {
        $result = php_unique_types($test_array);
    }
    $php_time = microtime(true) - $start;

    // Benchmark C implementation
    $start = microtime(true);
    for ($i = 0; $i < $iterations; $i++) {
        $result = phan_unique_types($test_array);
    }
    $c_time = microtime(true) - $start;

    // Verify results match
    $php_result = php_unique_types($test_array);
    $c_result = phan_unique_types($test_array);

    if (count($php_result) !== count($c_result)) {
        echo "ERROR: Result count mismatch!\n";
        return;
    }

    echo "PHP implementation:  " . number_format($php_time, 4) . "s\n";
    echo "C implementation:    " . number_format($c_time, 4) . "s\n";
    echo "Speedup:             " . number_format($php_time / $c_time, 2) . "x\n";
    echo "Result count:        " . count($c_result) . "\n";
    echo "\n";
}

echo "\n";
echo "PHAN_HELPERS BENCHMARK\n";
echo "======================\n";
echo "\n";

// Small arrays (where PHP uses in_array)
benchmark(5, 3, 100000);
benchmark(7, 3, 100000);

// Threshold where PHP switches to spl_object_id
benchmark(8, 3, 50000);
benchmark(10, 3, 50000);

// Medium arrays
benchmark(50, 5, 10000);
benchmark(100, 5, 5000);

// Large arrays (realistic for Phan)
benchmark(500, 3, 1000);
benchmark(1000, 3, 500);

// Very large with high duplication (stress test)
benchmark(2000, 5, 100);

echo str_repeat("=", 70) . "\n";
echo "Benchmark complete!\n";
