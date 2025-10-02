# Phan Helpers Extension

A PHP extension to accelerate Phan static analysis through optimized C implementations of performance-critical functions.

## Overview

This extension provides native C implementations of Phan's most frequently called functions, with a focus on type system operations that dominate analysis time.

## Installation

```bash
phpize
./configure
make
sudo make install
```

Add to your php.ini:
```
extension=phan_helpers.so
```

Verify installation:
```bash
php --ri phan_helpers
```

## Functions

### `phan_unique_types(array $type_list): array`

Fast deduplication of Type objects using object handle-based hashing.

**Description**:
This function is a C implementation of `Phan\Language\UnionType::getUniqueTypes()`. It removes duplicate Type objects from an array using O(1) hash table lookups based on object identity.

**Performance**:
- O(n) time complexity (vs O(n²) for small arrays in PHP implementation)
- Constant time duplicate detection regardless of array size
- Minimal memory allocation overhead

**Usage in Phan**:
Replace calls to `UnionType::getUniqueTypes()` with:
```php
if (function_exists('phan_unique_types')) {
    $unique_types = phan_unique_types($type_list);
} else {
    $unique_types = UnionType::getUniqueTypes($type_list);
}
```

**Original PHP Implementation** (UnionType.php:368-385):
```php
public static function getUniqueTypes(array $type_list): array
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
```

**C Implementation Advantages**:
- No threshold-based algorithm switching (always uses hash table)
- Direct object handle access (no spl_object_id() function call overhead)
- Single pass through array (no array_values() call needed)
- Native hash table with optimized memory layout

## Benchmark

Test with varying array sizes of Type objects:

```php
<?php
// Create test Type objects
$types = [];
for ($i = 0; $i < 1000; $i++) {
    $types[] = new stdClass(); // Simulate Type objects
}

// Add duplicates
$test_array = array_merge($types, $types, $types);
shuffle($test_array);

// Benchmark
$start = microtime(true);
for ($i = 0; $i < 10000; $i++) {
    $result = phan_unique_types($test_array);
}
$c_time = microtime(true) - $start;

echo "C implementation: " . number_format($c_time, 4) . "s\n";
echo "Result count: " . count($result) . "\n";
```

## Development

### Building for Development

```bash
# Build with debug symbols
phpize
./configure --enable-debug
make
```

### Testing

```bash
# Run the test suite
make test

# Run specific test
php -d extension=modules/phan_helpers.so tests/001-basic.phpt
```

### Code Structure

- `phan_helpers.c` - Main implementation
- `php_phan_helpers.h` - Header file with function declarations
- `config.m4` - Autoconf configuration
- `tests/` - PHP test files (.phpt format)

## Roadmap

**Phase 1 (Current)**:
- ✅ `phan_unique_types()` - Type deduplication

**Phase 2**:
- `phan_intern_type()` - Type interning system
- `phan_can_cast()` - Type compatibility checking

**Phase 3**:
- `phan_map_*()` - FQSEN map operations
- `phan_parse_fqsen()` - FQSEN string parsing

## License

PHP License 3.01

## Author

Rasmus Lerdorf
