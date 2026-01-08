# Lodash Performance Optimization Plan

**Date:** 2026-01-03
**Status:** READY TO IMPLEMENT
**Priority:** HIGH - Blocking full lodash compilation

## Executive Summary

**Current State:**
- ✅ Lodash 17,100 lines: Loads in 0.11 seconds
- ❌ Full lodash 17,220 lines: Hangs after 30+ seconds
- 🎯 Root Cause: GC thrashing from excessive allocations in property access

**Target State:**
- 🎯 Full lodash loads in <5 seconds
- 🎯 60%+ reduction in memory allocations
- 🎯 2-3x faster property access

## Problem Analysis

### Root Cause: Allocation Storm During `runInContext()`

The final 120 lines of lodash call `runInContext()`, which:
1. Creates ~200+ lodash methods (map, filter, reduce, etc.)
2. Each method assigned as property: `lodash.map = function() {...}`
3. Property assignment triggers: `ts_object_get_prop(obj, "map")`
4. String key gets converted via `ts_value_get_string()` → `ts_string_from_value()`
5. **EVERY property access allocates a NEW TsString**

**Allocation Hotspots:**
```
ts_object_get_prop()           // Called 1000+ times during init
  └─ ts_value_get_string()     // Converts key to string
      └─ ts_string_from_value() // Allocates NEW TsString
          ├─ TsString::FromInt()    // For numeric keys
          └─ TsString::FromDouble() // For double keys
              └─ TsString::Create() // Calls ts_alloc()
                  └─ GC_malloc()    // Triggers GC when heap full
```

**Stack Trace Evidence:**
```
GC_mark_from+0x1a7                     // Stuck in mark phase
GC_mark_some_inner+0x1da                // Never completes
GC_stopped_mark+0xf0
GC_try_to_collect_inner+0x114
GC_collect_or_expand+0xc0
GC_allocobj+0xdd
ts_object_get_property+0x5a4           // Property access
user_main+0x305b8                      // Lodash runInContext()
```

## Optimization Strategy

### Phase 1: String Interning (HIGHEST IMPACT)

**Goal:** Eliminate duplicate string allocations
**Expected Impact:** 60-70% reduction in string allocations

#### 1.1 Global String Intern Table

**File:** `src/runtime/src/TsString.cpp`
**File:** `src/runtime/include/TsString.h`

Add global string cache:
```cpp
// In TsString.cpp
static std::unordered_map<std::string, TsString*> globalStringCache;
static const size_t MAX_INTERN_SIZE = 256; // Only intern short strings

TsString* TsString::GetInterned(const char* utf8Str) {
    if (!utf8Str) return Create(""); // Fallback
    
    size_t len = std::strlen(utf8Str);
    if (len > MAX_INTERN_SIZE) return Create(utf8Str); // Too big, don't intern
    
    std::string key(utf8Str);
    auto it = globalStringCache.find(key);
    if (it != globalStringCache.end()) {
        return it->second; // Cache hit!
    }
    
    // Cache miss - create and store
    TsString* str = Create(utf8Str);
    globalStringCache[key] = str;
    return str;
}
```

**Locations to Update:**
- `ts_object_get_property(obj, keyStr)` - intern `keyStr` before lookup
- `ts_value_get_string()` - use `GetInterned()` instead of `Create()`
- `TsString::FromInt()` - already has partial cache (0-999), expand to use global cache
- `TsString::FromDouble()` - add caching for common double values

**Testing:**
```typescript
// Test string interning
const obj: any = {};
for (let i = 0; i < 1000; i++) {
    obj["prop" + i] = i; // Should reuse "prop0", "prop1", etc.
}
console.log("Interning test passed");
```

#### 1.2 Compile-Time String Interning

**Goal:** Generate string constants at compile time for literal property access

**File:** `src/compiler/Codegen_Expressions.cpp` (property access codegen)

When generating code for `obj.propertyName`:
```cpp
// Current: Creates string at runtime
llvm::Value* keyStr = createString("propertyName"); // BAD: allocates every time

// Optimized: Create global string constant
llvm::GlobalVariable* internedStr = getOrCreateInternedString("propertyName");
llvm::Value* keyStr = builder->CreateLoad(internedStr); // Reuse constant
```

**Implementation:**
- Add `std::map<std::string, llvm::GlobalVariable*> internedStrings` to CodeGenerator
- Method: `llvm::GlobalVariable* getOrCreateInternedString(const std::string& str)`
- Generate calls to `TsString::GetInterned()` at module init time
- Store results in global variables

**Expected Impact:** 40-50% faster static property access

---

### Phase 2: Optimize Property Access Fast Paths

**Goal:** Reduce overhead in `ts_object_get_prop`
**Expected Impact:** 30-40% faster property lookups

#### 2.1 Skip String Conversion for Numeric Array Access

**File:** `src/runtime/src/TsObject.cpp` - `ts_object_get_prop()`

Current code converts numeric keys to strings even for arrays:
```cpp
// Current: Always converts to string
TsString* keyStr = (TsString*)ts_value_get_string(key); // BAD: allocates

// Optimized: Fast path for array numeric access
if (key->type == ValueType::NUMBER_INT || key->type == ValueType::NUMBER_DBL) {
    void* rawObj = ts_value_get_object(obj);
    if (rawObj) {
        uint32_t magic = *(uint32_t*)rawObj;
        if (magic == 0x41525259) { // TsArray::MAGIC
            int64_t idx = (key->type == ValueType::NUMBER_INT) ? key->i_val : (int64_t)key->d_val;
            return ts_array_get_as_value(rawObj, idx); // SKIP STRING CONVERSION!
        }
    }
}
// Fall through to string conversion only for objects
```

**This optimization already exists!** But needs to come BEFORE string conversion.

#### 2.2 Check if Key is Already a String

**File:** `src/runtime/src/TsObject.cpp` - `ts_value_get_string()`

```cpp
void* ts_value_get_string(TsValue* v) {
    if (!v) return nullptr;
    
    // Fast path: Already a TsString pointer
    if (v->type == ValueType::STRING_PTR) {
        return v->ptr_val; // No conversion needed!
    }
    
    // Check for raw TsString (magic check)
    uint32_t magic = *(uint32_t*)v;
    if (magic == 0x53545247) { // TsString::MAGIC
        return v; // Already a string!
    }
    
    // Slow path: Convert to string
    return ts_string_from_value(v); // Only allocate if necessary
}
```

**Expected Impact:** Eliminates conversions when key is already a string

---

### Phase 3: Reduce UTF-8 Buffer Allocations

**Goal:** Avoid allocating `utf8Buffer` on every `ToUtf8()` call
**Expected Impact:** 20-30% reduction in allocations

#### 3.1 Lazy UTF-8 Buffer Allocation

**File:** `src/runtime/src/TsString.cpp` - `ToUtf8()`

Current code allocates buffer even if already cached:
```cpp
const char* TsString::ToUtf8() {
    if (isSmall) return data.inlineBuffer; // GOOD
    
    if (data.heap.utf8Buffer) return data.heap.utf8Buffer; // GOOD - cached
    
    // BAD: Always allocates on first call
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(data.heap.impl);
    std::string str;
    s->toUTF8String(str); // Allocates temporary std::string
    
    size_t len = str.length();
    data.heap.utf8Buffer = (char*)ts_alloc(len + 1); // Allocates buffer
    std::memcpy(data.heap.utf8Buffer, str.c_str(), len + 1);
    return data.heap.utf8Buffer;
}
```

**Optimization:** Use stack buffer for small strings:
```cpp
const char* TsString::ToUtf8() {
    if (isSmall) return data.inlineBuffer;
    if (data.heap.utf8Buffer) return data.heap.utf8Buffer;
    
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(data.heap.impl);
    
    // OPTIMIZATION: Use stack buffer for small strings
    char stackBuffer[128];
    int32_t capacity = sizeof(stackBuffer);
    UErrorCode status = U_ZERO_ERROR;
    int32_t len = s->extract(stackBuffer, capacity, nullptr, status);
    
    if (U_SUCCESS(status) && len < capacity) {
        // Fits in stack buffer - allocate heap copy
        data.heap.utf8Buffer = (char*)ts_alloc(len + 1);
        std::memcpy(data.heap.utf8Buffer, stackBuffer, len + 1);
        return data.heap.utf8Buffer;
    }
    
    // Fallback: Large string, use std::string
    std::string str;
    s->toUTF8String(str);
    size_t strLen = str.length();
    data.heap.utf8Buffer = (char*)ts_alloc(strLen + 1);
    std::memcpy(data.heap.utf8Buffer, str.c_str(), strLen + 1);
    return data.heap.utf8Buffer;
}
```

---

### Phase 4: Numeric String Cache Expansion

**Goal:** Cache more numeric strings beyond 0-999
**Expected Impact:** 10-15% reduction in allocations

**Current Implementation:**
```cpp
static std::unordered_map<int64_t, TsString*> numericStringCache;
static const int MAX_CACHED_INT = 999;
static const int MIN_CACHED_INT = 0;
```

**Expand to:**
```cpp
static const int MAX_CACHED_INT = 9999;  // Cache 0-9999
static const int MIN_CACHED_INT = -100;  // Cache -100 to 9999
```

Also cache common doubles:
```cpp
TsString* TsString::FromDouble(double value) {
    // Check if it's an integer
    if (std::floor(value) == value && value >= MIN_CACHED_INT && value <= MAX_CACHED_INT) {
        return FromInt((int64_t)value); // Use int cache
    }
    
    // Cache common fractions
    static std::unordered_map<double, TsString*> doubleCache;
    auto it = doubleCache.find(value);
    if (it != doubleCache.end()) return it->second;
    
    // Create new
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", value);
    TsString* str = Create(buf);
    
    // Cache small values
    if (doubleCache.size() < 1000) {
        doubleCache[value] = str;
    }
    
    return str;
}
```

---

## Implementation Plan

### Week 1: Foundation (Tasks 105.13.1 - 105.13.2)
**Days 1-2:** Implement global string interning
- Add `TsString::GetInterned()` method
- Add `globalStringCache` map
- Update `ts_value_get_string()` to use interning
- Update `ts_object_get_property()` to intern keys
- **Test:** Run lodash 17,100 lines, verify performance improvement

**Days 3-4:** Optimize property access fast paths
- Move array numeric access check earlier
- Add "already a string" fast path in `ts_value_get_string()`
- **Test:** Run property access benchmarks

**Day 5:** Expand numeric string cache
- Increase range to -100 to 9999
- Add double caching
- **Test:** Measure allocation reduction

**Milestone:** Lodash 17,100 lines should be 30-40% faster

### Week 2: Advanced Optimizations (Tasks 105.13.3 - 105.13.4)
**Days 1-2:** UTF-8 buffer optimization
- Implement lazy allocation with stack buffer
- **Test:** Profile memory allocations

**Days 3-4:** Compile-time string interning
- Add `getOrCreateInternedString()` to CodeGenerator
- Generate global constants for literal property names
- Update property access codegen
- **Test:** Verify static property access uses constants

**Day 5:** Integration testing
- Run full lodash compilation
- Measure load time
- **Target:** <5 seconds for full lodash

### Week 3: Profiling and Tuning (Tasks 105.13.5 - 105.13.6)
**Days 1-2:** Performance profiling
- Use Windows Performance Analyzer
- Identify remaining hotspots
- Measure before/after metrics

**Days 3-4:** Final optimizations
- Address any remaining bottlenecks
- Tune GC parameters if needed

**Day 5:** Documentation
- Write performance guide
- Document optimization techniques
- Update epic with results

---

## Success Metrics

### Performance Targets
- ✅ Baseline (current): 17,100 lines in 0.11s
- 🎯 After Phase 1: 17,100 lines in 0.05s (2x faster)
- 🎯 After Phase 2: 17,220 lines in 3-4s (loads successfully)
- 🎯 After Phase 3: 17,220 lines in 1-2s (optimized)
- 🎯 After Phase 4: 17,220 lines in <1s (production ready)

### Memory Targets
- 🎯 60%+ reduction in total allocations
- 🎯 String allocations reduced by 70%+
- 🎯 Peak memory usage <512MB for lodash load
- 🎯 No GC thrashing (collections complete within 100ms)

### Quality Targets
- ✅ All optimizations covered by tests
- ✅ No regressions in existing functionality
- ✅ Performance guide documentation complete
- ✅ Lodash full test suite passes

---

## Fallback Plan

If optimizations are insufficient to load full lodash in <5s:

### Option A: Lazy Initialization
Add `--lazy-init` compiler flag:
- Defer method creation until first use
- Create lodash object with getters that initialize on demand
- Expected: Instant load, slight overhead on first method call

### Option B: Modular Lodash
- Split lodash into ES6 modules (one per method)
- Tree-shake unused methods
- Expected: Much smaller bundle, faster load

### Option C: Lodash Lite
- Create curated subset of most-used methods
- Document as "ts-aot optimized lodash"
- Expected: Full performance, 80% of use cases covered

---

## Risk Assessment

### Low Risk
- ✅ String interning: Well-understood technique, minimal risk
- ✅ Numeric cache expansion: Already working for 0-999
- ✅ Fast path optimizations: Additive, don't break existing code

### Medium Risk
- ⚠️ Compile-time interning: Requires careful LLVM global management
- ⚠️ UTF-8 buffer changes: Must ensure thread safety

### High Risk
- 🔴 GC tuning: Could cause memory exhaustion if too aggressive

**Mitigation:**
- Implement incrementally with testing at each step
- Keep GC debug output for monitoring
- Add performance regression tests

---

## Resources Required

### Development Time
- Week 1: 5 days (foundation)
- Week 2: 5 days (advanced)
- Week 3: 5 days (tuning)
- **Total:** 15 days

### Tools Needed
- ✅ Windows Performance Analyzer (installed)
- ✅ CDB debugger (installed)
- ✅ attach_and_break.ps1 (created)
- ✅ Test harness (existing)

### Knowledge Required
- ✅ Boehm GC internals (documented)
- ✅ LLVM global constants (known)
- ✅ ICU string handling (in use)
- ⚠️ Performance profiling (learning required)

---

## Conclusion

This optimization plan addresses the root cause of lodash's performance issues through systematic improvements to string handling and property access. The phased approach allows for incremental validation and provides fallback options if full optimization proves insufficient.

**Next Action:** Begin Task 105.13.1 - Implement global string interning
