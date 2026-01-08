# Lodash Optimization Quick Reference

## Problem Statement
**Symptom:** Full lodash (17,220 lines) hangs after 30+ seconds during initialization
**Root Cause:** GC thrashing from excessive string allocations in property access
**Bottleneck:** `runInContext()` function (last 120 lines) creates 200+ methods

## Stack Trace Analysis
```
GC_mark_from+0x1a7                    ← Stuck in GC mark phase
GC_collect_or_expand+0xc0             ← Can't complete collection
ts_object_get_property+0x5a4          ← Property access
TsString::FromDouble+0x6               ← String allocation
user_main+0x305b8                     ← lodash runInContext()
```

## Allocation Hotspot
```cpp
lodash.map = function() {...}         // Property assignment
  ↓
ts_object_get_prop(obj, "map")        // Property access
  ↓
ts_value_get_string(key)              // Key conversion
  ↓
TsString::FromInt/FromDouble()        // String allocation
  ↓
TsString::Create() → ts_alloc()       // GC allocation
  ↓
GC_malloc() → GC_collect()            // Triggers GC
  ↓
THRASHING (can't keep up)             // Infinite loop!
```

## Optimization Roadmap

### Phase 1: Global String Interning [Week 1]
**File:** `src/runtime/src/TsString.cpp`
**Impact:** 60-70% fewer string allocations

```cpp
// Add global cache
static std::unordered_map<std::string, TsString*> globalStringCache;

TsString* TsString::GetInterned(const char* utf8Str) {
    auto it = globalStringCache.find(utf8Str);
    if (it != globalStringCache.end()) return it->second;
    
    TsString* str = Create(utf8Str);
    globalStringCache[utf8Str] = str;
    return str;
}
```

**Update locations:**
- `ts_object_get_property()` - intern all property names
- `ts_value_get_string()` - use GetInterned instead of Create
- `TsString::FromInt/FromDouble()` - route through global cache

### Phase 2: Property Access Fast Paths [Week 2]
**File:** `src/runtime/src/TsObject.cpp`
**Impact:** 30-40% faster lookups

```cpp
// In ts_object_get_prop():
// 1. Check if already a string (skip conversion)
if (key->type == ValueType::STRING_PTR) {
    // Use directly, no conversion!
}

// 2. For arrays, skip string conversion entirely
if (isArray && isNumericKey) {
    return ts_array_get_as_value(arr, idx); // Direct access
}
```

### Phase 3: UTF-8 Buffer Optimization [Week 2]
**File:** `src/runtime/src/TsString.cpp`
**Impact:** 20-30% fewer allocations

```cpp
// Use stack buffer for small strings
const char* TsString::ToUtf8() {
    if (data.heap.utf8Buffer) return data.heap.utf8Buffer;
    
    char stackBuffer[128];
    int32_t len = s->extract(stackBuffer, 128, nullptr, status);
    
    if (U_SUCCESS(status)) {
        // Allocate once and cache
        data.heap.utf8Buffer = (char*)ts_alloc(len + 1);
        memcpy(data.heap.utf8Buffer, stackBuffer, len + 1);
    }
    return data.heap.utf8Buffer;
}
```

### Phase 4: Compile-Time Interning [Week 2-3]
**File:** `src/compiler/Codegen_Expressions.cpp`
**Impact:** 40-50% faster static property access

```cpp
// Generate global string constants for literal properties
llvm::GlobalVariable* getOrCreateInternedString(const std::string& name) {
    if (internedStrings.count(name)) return internedStrings[name];
    
    // Create global constant
    llvm::GlobalVariable* gv = createGlobalString(name);
    internedStrings[name] = gv;
    return gv;
}

// In property access codegen:
// obj.foo → use global constant, not runtime allocation
```

## Testing Strategy

### Incremental Testing
```typescript
// Test 1: String interning
const obj: any = {};
for (let i = 0; i < 10000; i++) {
    obj["prop" + i] = i;  // Should reuse strings
}

// Test 2: Numeric property access
const arr: any[] = new Array(1000);
for (let i = 0; i < 1000; i++) {
    arr[i] = i * 2;  // Should skip string conversion
}

// Test 3: Full lodash
const _ = require("lodash");  // Target: <5 seconds
```

### Performance Benchmarks
| Metric | Baseline | Phase 1 | Phase 2 | Phase 3 | Phase 4 | Target |
|--------|----------|---------|---------|---------|---------|--------|
| Load time (17,220 lines) | >30s | 10s | 5s | 3s | <1s | <5s |
| String allocations | 100% | 30% | 25% | 20% | 15% | <40% |
| Property access speed | 1x | 1.5x | 2x | 2.5x | 3x | >2x |

## Success Criteria
- ✅ Full lodash loads in <5 seconds
- ✅ 60%+ reduction in memory allocations
- ✅ 2-3x faster property access
- ✅ No regressions in existing code
- ✅ Performance guide documented

## Debugging Tools
```powershell
# Capture stack trace from hung process
.\attach_and_break.ps1

# Run with timeout
$proc = Start-Process -FilePath "test.exe" -NoNewWindow -PassThru
$proc | Wait-Process -Timeout 30 -ErrorAction SilentlyContinue
if (-not $proc.HasExited) { $proc.Kill(); "HUNG" } else { "SUCCESS" }

# Bisect lodash to find bottleneck
Get-Content lodash.js | Select-Object -First 17100 > lodash_17100.js
```

## Files Modified
- ✅ `src/runtime/src/Memory.cpp` - GC configuration
- ✅ `src/runtime/src/TsString.cpp` - Numeric string cache
- 🔄 `src/runtime/src/TsString.cpp` - Global string interning (Phase 1)
- 🔄 `src/runtime/src/TsObject.cpp` - Property access fast paths (Phase 2)
- 🔄 `src/runtime/src/TsString.cpp` - UTF-8 buffer optimization (Phase 3)
- 🔄 `src/compiler/Codegen_Expressions.cpp` - Compile-time interning (Phase 4)

## References
- Epic: `docs/phase19/epic_105_lodash.md`
- Plan: `docs/phase19/lodash_optimization_plan.md`
- Summary: `docs/phase19/lodash_progress_summary.md`
- This card: `docs/phase19/lodash_optimization_quickref.md`
