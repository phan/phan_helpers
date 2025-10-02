# Phan Helpers Benchmark Results

**Date**: 2025-10-02
**PHP Version**: 8.4.14-dev
**Extension Version**: 0.1.0

## Summary

The `phan_unique_types()` C implementation demonstrates **2.65x to 3.72x speedup** over the PHP implementation across all tested array sizes.

## Benchmark Results

| Array Size | Unique Objects | Duplication | Iterations | PHP Time | C Time | Speedup |
|------------|----------------|-------------|------------|----------|--------|---------|
| 15         | 5              | 3x          | 100,000    | 0.0564s  | 0.0189s | **2.99x** |
| 21         | 7              | 3x          | 100,000    | 0.0701s  | 0.0258s | **2.72x** |
| 24         | 8              | 3x          | 50,000     | 0.0395s  | 0.0138s | **2.86x** |
| 30         | 10             | 3x          | 50,000     | 0.0496s  | 0.0173s | **2.87x** |
| 250        | 50             | 5x          | 10,000     | 0.0697s  | 0.0187s | **3.72x** |
| 500        | 100            | 5x          | 5,000      | 0.0687s  | 0.0187s | **3.67x** |
| 1,500      | 500            | 3x          | 1,000      | 0.0440s  | 0.0152s | **2.89x** |
| 3,000      | 1,000          | 3x          | 500        | 0.0438s  | 0.0165s | **2.65x** |
| 10,000     | 2,000          | 5x          | 100        | 0.0278s  | 0.0085s | **3.27x** |

## Analysis

### Performance Characteristics

1. **Small Arrays (5-10 unique objects)**:
   - Speedup: **2.72x - 2.99x**
   - PHP uses `in_array()` with O(n²) complexity
   - C uses hash table with O(n) complexity

2. **Medium Arrays (50-100 unique objects)**:
   - Speedup: **3.67x - 3.72x** (best performance)
   - PHP switches to `spl_object_id()` but still has overhead
   - C maintains constant-time lookups

3. **Large Arrays (500-2000 unique objects)**:
   - Speedup: **2.65x - 3.27x**
   - Both implementations use object ID hashing
   - C benefits from native hash table and reduced allocations

### Key Insights

- **Consistent Performance**: Speedup remains in the 2.5x-3.7x range across all sizes
- **Best Performance**: Medium-sized arrays (50-100 objects) show highest speedup
- **Scalability**: Performance advantage maintained even at 2000+ objects
- **No Threshold**: C implementation doesn't need threshold-based algorithm switching

## Real-World Impact on Phan

### Expected Improvements

Based on these results, integrating `phan_unique_types()` into Phan should yield:

1. **Direct Impact**:
   - Every `UnionType::getUniqueTypes()` call runs 2.7-3.7x faster
   - Called thousands of times during analysis

2. **Estimated Overall Speedup**:
   - Conservative: **5-10% faster analysis** (if type deduplication is 15-30% of workload)
   - Optimistic: **10-15% faster analysis** (if type operations dominate)

3. **Compounding Effects**:
   - Reduced GC pressure from fewer temporary arrays
   - Better CPU cache utilization
   - More time available for actual analysis logic

### Integration Path

1. **Phase 1: Drop-in Replacement**
   ```php
   // In UnionType.php
   public static function getUniqueTypes(array $type_list): array
   {
       if (function_exists('phan_unique_types')) {
           return phan_unique_types($type_list);
       }
       // Fallback to PHP implementation
       // ... existing code ...
   }
   ```

2. **Phase 2: Measure Impact**
   - Benchmark Phan self-analysis
   - Test on large real-world projects
   - Verify correctness with full test suite

3. **Phase 3: Optimize Further**
   - Identify next bottlenecks
   - Consider additional C implementations per PHAN_HELPER.md roadmap

## Technical Notes

### Implementation Details

- **Hash Function**: Uses `Z_OBJ_HANDLE_P()` for direct object handle access
- **Memory Management**: Single hash table allocation, cleaned up properly
- **Edge Cases**: Handles non-object values gracefully (though rare in Phan)
- **Thread Safety**: No global state, safe for ZTS builds

### Build Information

```
Platform: Linux x86_64
Compiler: gcc with -O2 optimization
PHP Version: 8.4.14-dev
Zend API: 20240924
```

### Test Coverage

- ✅ Empty arrays
- ✅ Single element arrays
- ✅ Arrays with duplicates
- ✅ Large arrays (1000+ objects)
- ✅ Order preservation
- ✅ Different object instances of same class

## Conclusion

The proof-of-concept demonstrates that C implementations of Phan's hot-path functions can deliver **substantial performance improvements** with minimal code changes required in Phan itself.

**Recommendation**: Proceed with integration and measurement on real Phan workloads.

---

*Generated from benchmark.php run on 2025-10-02*
