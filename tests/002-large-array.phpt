--TEST--
phan_unique_types() with large arrays
--SKIPIF--
<?php if (!extension_loaded("phan_helpers")) print "skip"; ?>
--FILE--
<?php
class TestType {}

// Create 1000 unique objects
$unique_objects = [];
for ($i = 0; $i < 1000; $i++) {
    $unique_objects[] = new TestType();
}

// Create array with many duplicates
$test_array = [];
for ($i = 0; $i < 5; $i++) {
    foreach ($unique_objects as $obj) {
        $test_array[] = $obj;
    }
}

shuffle($test_array);

echo "Input size: " . count($test_array) . "\n";

$result = phan_unique_types($test_array);

echo "Output size: " . count($result) . "\n";

// Verify all unique objects are present
$found_all = true;
foreach ($unique_objects as $obj) {
    if (!in_array($obj, $result, true)) {
        $found_all = false;
        break;
    }
}

echo "All unique objects present: " . ($found_all ? "yes" : "no") . "\n";

// Verify no duplicates in result
$ids = [];
foreach ($result as $obj) {
    $id = spl_object_id($obj);
    if (isset($ids[$id])) {
        echo "FAIL: Duplicate found!\n";
        exit(1);
    }
    $ids[$id] = true;
}

echo "No duplicates in result: yes\n";
echo "OK\n";
?>
--EXPECT--
Input size: 5000
Output size: 1000
All unique objects present: yes
No duplicates in result: yes
OK
