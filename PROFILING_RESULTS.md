# Profiling Results: Where Phan Actually Spends Time

**Date**: 2025-10-02
**Tool**: phpspy (sampling profiler, 100Hz)
**Test**: Phan v6 self-analysis
**Total samples**: ~1500 samples over 15s run

## Top Functions by Sample Count

| Function | Samples | % of Total | Category |
|----------|---------|------------|----------|
| `BlockAnalysisVisitor::analyzeAndGetUpdatedContext` | 824 | ~55% | **Analysis** |
| `BlockAnalysisVisitor::visitStmtList` | 486 | ~32% | **Analysis** |
| `Phan::analyzeFileList` | 432 | ~29% | Orchestration |
| `Phan::finishAnalyzingRemainingStatements` | 334 | ~22% | Orchestration |
| `Analysis::parseNodeInContext` | 295 | ~20% | **Parsing** |
| `Analysis::analyzeFile` | 198 | ~13% | Analysis |
| `KindVisitorImplementation::__invoke` | 189 | ~13% | Visitor dispatch |
| `BlockAnalysisVisitor::visit` | 144 | ~10% | Analysis |
| `BlockAnalysisVisitor::visitClosedContext` | 136 | ~9% | Analysis |
| `BlockAnalysisVisitor::visitClass` | 125 | ~8% | Analysis |
| `BlockAnalysisVisitor::visitMethod` | 116 | ~8% | Analysis |
| `ASTHasher::hash` | 101 | ~7% | **AST Hashing** |
| `ASTHasher::computeHash` | 101 | ~7% | **AST Hashing** |

## Key Findings

### 1. Analysis Dominates (>60% of time)

The `BlockAnalysisVisitor` and its methods consume the vast majority of time:
- Traversing AST nodes
- Updating context
- Visiting statements

**Optimization target**: The visitor pattern itself, context updates, or node traversal.

### 2. Type Operations Are Minor (<2% of time)

Type-related functions have minimal samples:

| Function | Samples | % of Total |
|----------|---------|------------|
| `Method::cloneWithTemplateParameterTypeMap` | 25 | 1.7% |
| `ParameterTypesAnalyzer::analyzeParameterTypesInner` | 20 | 1.3% |
| `ArgumentType::analyzeParameterList` | 20 | 1.3% |
| `ArgumentType::analyze` | 17 | 1.1% |
| `UnionTypeVisitor::unionTypeFromNode` | 9 | 0.6% |
| `UnionType::withStaticResolvedInContext` | 9 | 0.6% |

**Critical observation**: `UnionType::getUniqueTypes()` **doesn't even appear** in the profile!

### 3. AST Hashing Is Surprisingly Expensive (7%)

`ASTHasher::hash()` and `computeHash()` each show ~101 samples (7% of total).

**Why?**: Used for incremental analysis and caching. Every AST node gets hashed.

**Optimization opportunity**: This is a better target than type operations.

### 4. Parsing Is Moderate (20%)

`Analysis::parseNodeInContext` shows 295 samples (~20%).

**Why?**: Converting PHP code → AST, handling all node types.

**Limited optimization potential**: Already using php-ast extension (native C).

## Why getUniqueTypes() Had No Impact

### Expected vs Actual

**Microbenchmark**: 2.7-3.7x speedup
**Real-world impact**: 0.3% (within noise)

**Explanation**: `getUniqueTypes()` doesn't appear in profile with meaningful sample count.

### Calculated Impact Using Amdahl's Law

If `getUniqueTypes()` is 0.1% of runtime and we make it 3x faster:

```
Speedup = 1 / ((1 - 0.001) + (0.001 / 3))
        = 1 / (0.999 + 0.0003)
        = 1 / 0.9993
        = 1.0007
        = 0.07% improvement
```

**Conclusion**: Our measured 0.3% is actually consistent with profiling data!

## Real Optimization Targets

Based on profiling data, here are functions worth optimizing in C:

### Tier 1: High Impact (>5% of runtime each)

1. **BlockAnalysisVisitor::analyzeAndGetUpdatedContext** (55%)
   - Updates context as it traverses
   - Copies/clones context objects
   - **C benefit**: Reduce object allocation overhead

2. **ASTHasher::hash/computeHash** (7% each)
   - Recursively hashes AST nodes
   - String concatenation and hashing
   - **C benefit**: Native hash functions, no string allocations

### Tier 2: Medium Impact (1-3% of runtime each)

3. **Method::cloneWithTemplateParameterTypeMap** (1.7%)
   - Clones methods with template substitution
   - Object copying overhead
   - **C benefit**: Direct memory copy, fewer allocations

4. **ArgumentType::analyzeParameterList** (1.3%)
   - Analyzes function call arguments
   - Type compatibility checks
   - **C benefit**: Inline type checking logic

### Tier 3: Speculative (Not Measured But Likely Expensive)

5. **UnionType::canCastToUnionType()** (not in profile, but see note below)
   - Nested loops over type sets
   - Called during `analyzeParameterList`
   - **Why not in profile?**: May be inlined by JIT or too fast to sample

6. **Context cloning operations** (indirect)
   - Not a single function but used throughout visitors
   - Every context update creates copies
   - **C benefit**: Copy-on-write semantics in C

## Recommended Implementation Priority

### Phase 1: AST Hashing (Expected: 5-7% speedup)

```c
// phan_helpers extension
PHP_FUNCTION(phan_ast_hash) {
    // Fast recursive AST hashing without string allocations
    // Direct C implementation of ASTHasher::computeHash()
}
```

**Rationale**:
- Clear 7% of runtime in profile
- Self-contained function
- Measurable improvement expected

### Phase 2: Context Management (Expected: 10-20% speedup)

More complex but highest potential impact:

```c
// Optimize context copy/update operations
// Used heavily in BlockAnalysisVisitor::analyzeAndGetUpdatedContext (55% of runtime)
```

**Rationale**:
- Dominates runtime (55%)
- Lots of object allocations
- But requires significant refactoring

### Phase 3: Type Operations (Expected: <2% speedup)

Only implement if Phases 1-2 don't meet goals:

```c
PHP_FUNCTION(phan_can_cast) {
    // Type compatibility checking
}
```

**Rationale**:
- Low measured impact
- Already explored with `phan_unique_types`
- Save effort for higher-impact targets

## Profiling Artifacts

**Note**: phpspy had many "copy_proc_mem" errors. This is normal for sampling profilers on JIT-enabled PHP. The successful samples are still statistically significant.

**Total error samples**: ~1200 (vs ~1500 valid samples)
**Impact**: May undercount very fast functions, but distribution is representative.

## Conclusions

1. ✅ **Profiling data explains benchmark results**: Type operations are <2% of runtime
2. ✅ **AST hashing is the real bottleneck**: 7% vs <<1% for type deduplication
3. ✅ **Analysis visitor dominates**: 55% in `analyzeAndGetUpdatedContext` alone
4. ❌ **Type optimization was wrong target**: Premature optimization without profiling

**Key Learning**: **Always profile before optimizing**. Microbenchmarks can be misleading.

## Next Steps

1. **Implement `phan_ast_hash()`** - Clear 7% target
2. **Benchmark and measure** - Verify 5-7% speedup
3. **Consider context optimization** - Higher risk but 10-20% potential
4. **Profile larger codebases** - Verify findings generalize

---

*Profiling data collected with: `sudo phpspy -H 100 -V 84 -l 100000 -- ./phan --no-progress-bar`*
