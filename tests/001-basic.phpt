--TEST--
phan_unique_types() basic functionality
--SKIPIF--
<?php if (!extension_loaded("phan_helpers")) print "skip"; ?>
--FILE--
<?php
// Test with objects
class Type1 {}
class Type2 {}

$obj1 = new Type1();
$obj2 = new Type2();
$obj3 = new Type1();

// Test empty array
$result = phan_unique_types([]);
var_dump($result);

// Test single element
$result = phan_unique_types([$obj1]);
var_dump(count($result));
var_dump($result[0] === $obj1);

// Test with duplicates (same object references)
$result = phan_unique_types([$obj1, $obj2, $obj1, $obj2, $obj1]);
var_dump(count($result));
var_dump($result[0] === $obj1);
var_dump($result[1] === $obj2);

// Test with different objects of same class
$result = phan_unique_types([$obj1, $obj3]);
var_dump(count($result));  // Should be 2 (different object instances)

// Test preserves order of first occurrence
$result = phan_unique_types([$obj2, $obj1, $obj2, $obj1]);
var_dump($result[0] === $obj2);
var_dump($result[1] === $obj1);

echo "OK\n";
?>
--EXPECT--
array(0) {
}
int(1)
bool(true)
int(2)
bool(true)
bool(true)
int(2)
bool(true)
bool(true)
OK
