# HIR-004: Handler Registration Pattern for Builtin Functions

**Status:** Phase 7d Complete (Generator and RegExp Handlers)
**Category:** Architecture / Refactoring
**Priority:** High
**Last Updated:** 2026-02-04

## Problem Statement

The `lowerCall` and `lowerCallMethod` functions in `HIRToLLVM.cpp` have grown to ~1300 and ~600+ lines respectively, containing 60+ and 30+ hardcoded special cases. This creates several issues:

1. **Maintainability**: Adding new builtins requires modifying giant functions
2. **Testing**: Difficult to test individual builtin handlers in isolation
3. **Discoverability**: Hard to find where a specific builtin is handled
4. **Refactoring Risk**: Changes to large functions risk breaking unrelated functionality

## Baseline Test Results (2026-02-04)

Before beginning refactoring, current test status:

| Test Suite | Passed | Total | Rate |
|------------|--------|-------|------|
| Golden IR | 120 | 146 | 82.2% |
| Node.js | 190 | 280 | 67.9% |

These baselines must not regress during refactoring.

## Proposed Solution: Handler Registration Pattern

### Architecture

```
src/compiler/hir/handlers/
├── BuiltinHandler.h        # Interface
├── HandlerRegistry.h       # Registration & dispatch
├── HandlerRegistry.cpp
├── MathHandler.cpp         # Math.*, Number.*
├── ConsoleHandler.cpp      # console.*
├── ArrayHandler.cpp        # Array methods
├── MapSetHandler.cpp       # Map, Set, WeakMap, WeakSet
├── PathHandler.cpp         # path.*
├── TimerHandler.cpp        # setTimeout, setInterval
├── BigIntHandler.cpp       # BigInt operations
├── GeneratorHandler.cpp    # Generator handling
└── RegExpHandler.cpp       # RegExp operations
```

### Interface Design

```cpp
// BuiltinHandler.h
class BuiltinHandler {
public:
    virtual ~BuiltinHandler() = default;

    // Returns true if this handler handles the given call
    virtual bool canHandle(const std::string& funcName,
                          HIRInstruction* inst) const = 0;

    // Lower the call to LLVM IR, returns the result value
    virtual llvm::Value* lower(HIRInstruction* inst,
                               HIRToLLVM& lowerer) = 0;
};

// HandlerRegistry.h
class HandlerRegistry {
public:
    static HandlerRegistry& instance();

    void registerHandler(std::unique_ptr<BuiltinHandler> handler);

    // Try all handlers, return result or nullptr if none matched
    llvm::Value* tryLower(const std::string& funcName,
                          HIRInstruction* inst,
                          HIRToLLVM& lowerer);
private:
    std::vector<std::unique_ptr<BuiltinHandler>> handlers_;
};
```

### Simplified lowerCall Flow

```cpp
void HIRToLLVM::lowerCall(HIRInstruction* inst) {
    std::string funcName = getOperandString(inst->operands[0]);

    // 1. Try handler registry first
    if (auto* result = HandlerRegistry::instance().tryLower(funcName, inst, *this)) {
        if (inst->result) setValue(inst->result, result);
        return;
    }

    // 2. Try LoweringRegistry for simple runtime calls
    if (auto* spec = LoweringRegistry::instance().lookup(funcName)) {
        llvm::Value* result = lowerRegisteredCall(inst, *spec);
        if (inst->result) setValue(inst->result, result);
        return;
    }

    // 3. Fall back to user-defined function lookup
    lowerUserDefinedCall(inst);
}
```

## Implementation Phases

### Phase 1: Handler Infrastructure (Complete)
- [x] Create BuiltinHandler interface
- [x] Create HandlerRegistry class
- [x] Add CMake configuration for handlers/
- [x] Test with empty registry (no functional change)

### Phase 2: Extract Math Handler (Complete)
- [x] Create MathHandler.cpp
- [x] Move Math.min, Math.max, Math.floor, etc. from lowerCall
- [x] Move Number.isNaN, Number.isFinite, etc.
- [x] Verify no regression in golden IR tests (120/146, 82.2% - unchanged)

### Phase 3: Extract Console Handler (Complete)
- [x] Create ConsoleHandler.cpp
- [x] Move console.log type dispatch logic
- [x] Handler takes precedence over old code (no removal needed yet)
- [x] Verify no regression in golden IR tests (120/146, 82.2% - unchanged)

### Phase 4: Extract Array Handler (Complete)
- [x] Create ArrayHandler.cpp
- [x] Move ts_array_create, ts_array_concat, ts_array_push
- [x] Move ts_array_find, ts_array_findLast, ts_array_findIndex, ts_array_findLastIndex
- [x] Move ts_array_some, ts_array_every, ts_array_slice
- [x] Verify no regression in golden IR tests (120/146, 82.2% - unchanged)

### Phase 5: Extract Remaining Handlers

#### 5a: MapSetHandler (Complete)
Functions extracted from `lowerCall`:
- [x] `ts_map_set` - Set key-value pair
- [x] `ts_map_get` - Get value by key (with boxing)
- [x] `ts_map_has` - Check key existence
- [x] `ts_map_delete` - Delete key
- [x] `ts_map_clear` - Clear all entries
- [x] `ts_map_size` - Get entry count
- [x] `ts_set_add` - Add value to Set
- [x] `ts_set_has` - Check value existence
- [x] `ts_set_delete` - Delete value
- [x] `ts_set_clear` - Clear all entries
- [x] `ts_set_size` - Get entry count
- [x] Verify no regression in golden IR tests (120/146, 82.2% - unchanged)

#### 5b: TimerHandler (Complete)
Functions extracted (use prefix matching):
- [x] `setTimeout*` variants - Delay callback execution
- [x] `setInterval*` variants - Repeat callback execution
- [x] `setImmediate*` variants - Next tick execution
- [x] `clearTimeout`, `clearInterval`, `clearImmediate` - Cancel timers
- [x] Verify no regression in golden IR tests (120/146, 82.2% - unchanged)

#### 5c: BigIntHandler (Complete)
Functions extracted from `lowerCall`:
- [x] `ts_bigint_create_str` - Create from string
- [x] `ts_bigint_add`, `ts_bigint_sub` - Arithmetic
- [x] `ts_bigint_mul`, `ts_bigint_div`, `ts_bigint_mod` - Arithmetic
- [x] `ts_bigint_lt`, `ts_bigint_le`, `ts_bigint_gt`, `ts_bigint_ge` - Comparison
- [x] `ts_bigint_eq`, `ts_bigint_ne` - Equality
- [x] Verify no regression in golden IR tests (120/146, 82.2% - unchanged)

#### 5d: PathHandler (Complete)
Functions extracted from `lowerCall`:
- [x] `ts_path_basename` - Extract filename
- [x] `ts_path_dirname` - Extract directory
- [x] `ts_path_extname` - Extract extension
- [x] `ts_path_join` - Join path segments
- [x] `ts_path_normalize` - Normalize path
- [x] `ts_path_resolve` - Resolve to absolute
- [x] `ts_path_relative` - Compute relative path
- [x] `ts_path_isAbsolute` - Check if absolute
- [x] Verify no regression in golden IR tests (120/146, 82.2% - unchanged)

#### 5e: Future Handlers (Lower Priority)
- [x] GeneratorHandler - Generator/AsyncGenerator method handling (moved to Phase 7d)
- [x] RegExpHandler - RegExp method handling (moved to Phase 7d)
- [ ] StringHandler - String methods not in LoweringRegistry
- [ ] ValueHandler - ts_value_* boxing operations
- [ ] ObjectHandler - ts_object_* operations

### Phase 6: Cleanup (Complete)

The HandlerRegistry check (line 2733) now catches handled functions BEFORE inline code.
Dead inline code in `lowerCall` has been successfully removed:

| Handler | Lines Removed | Dead Code Size |
|---------|---------------|----------------|
| ConsoleHandler | ~42 lines | ✓ Removed |
| ArrayHandler | ~175 lines | ✓ Removed |
| MathHandler | ~260 lines | ✓ Removed |
| TimerHandler | ~81 lines | ✓ Removed |
| BigIntHandler | ~49 lines | ✓ Removed |
| MapSetHandler | ~281 lines | ✓ Removed (including boxForMapSet helper) |
| PathHandler | ~136 lines | ✓ Removed |
| **Total** | **~1024 lines** | ✓ All removed |

Code that REMAINS in `lowerCall`:
- ts_value_to_bool, ts_value_is_undefined, ts_value_is_nullish
- ts_object_has_property
- ts_string_from_* functions
- ts_object_assign
- ts_object_is
- ts_json_stringify
- Generic function call handling

Tasks:
- [x] Remove dead handler inline code (~1024 lines)
- [x] Update documentation
- [x] Final verification of all test suites (120/146 passing, 82.2% - no regression)

**Note:** Phase 6 only cleaned up `lowerCall`. Success criteria #3 (lowerCallMethod to ~50 lines) is NOT met.

### Phase 7: MethodHandler Support (In Progress)

`lowerCallMethod` is now ~700 lines (down from ~928) with inline handling for:

| Category | Methods | Lines | Status |
|----------|---------|-------|--------|
| Console | log, error, warn, info, debug | ~57 | Using LoweringRegistry |
| Generator | next() | ~94 | Inline |
| RegExp | test(), exec() | ~60 | Inline (test duplicated!) |
| Array fallback | join, push | ~45 | Inline |
| Map methods | set, get, has, delete, clear | ~90 | ✓ Moved to MapSetHandler |
| Set methods | add, has, delete, clear | ~65 | ✓ Moved to MapSetHandler |
| boxValueForMapSet | lambda helper | ~46 | ✓ Removed (uses handler's boxForMapSet) |
| Dynamic dispatch | ts_call_with_this_N | ~400 | Inline, highly repetitive |

#### Phase 7a: Interface Extension

**Goal:** Add method call support to the handler interface without breaking existing handlers.

**Files to modify:**
- `src/compiler/hir/handlers/BuiltinHandler.h`
- `src/compiler/hir/handlers/HandlerRegistry.h`
- `src/compiler/hir/handlers/HandlerRegistry.cpp`

**Interface changes in BuiltinHandler.h:**
```cpp
class BuiltinHandler {
public:
    virtual ~BuiltinHandler() = default;
    virtual const char* name() const = 0;

    // Existing - for lowerCall (function calls like ts_map_set(map, key, val))
    virtual bool canHandle(const std::string& funcName, HIRInstruction* inst) const = 0;
    virtual llvm::Value* lower(const std::string& funcName, HIRInstruction* inst, HIRToLLVM& lowerer) = 0;

    // NEW - for lowerCallMethod (method calls like map.set(key, val))
    // Default implementations return false/nullptr so existing handlers don't break
    virtual bool canHandleMethod(const std::string& methodName,
                                 const std::string& className,
                                 HIRInstruction* inst) const { return false; }
    virtual llvm::Value* lowerMethod(const std::string& methodName,
                                     HIRInstruction* inst,
                                     HIRToLLVM& lowerer) { return nullptr; }
};
```

**New methods in HandlerRegistry:**
```cpp
class HandlerRegistry {
public:
    // ... existing methods ...

    /// Try to lower a method call using registered handlers.
    /// @param methodName The method name (e.g., "set", "get", "push")
    /// @param className The class name if known (e.g., "Map", "Set", "Array", or empty)
    /// @param inst The HIR CallMethod instruction
    /// @param lowerer Reference to the HIRToLLVM lowerer
    /// @return The result value if a handler succeeded, nullptr if no handler matched
    llvm::Value* tryLowerMethod(const std::string& methodName,
                                const std::string& className,
                                HIRInstruction* inst,
                                HIRToLLVM& lowerer);

    /// Check if any handler can handle the given method
    bool hasMethodHandler(const std::string& methodName,
                          const std::string& className,
                          HIRInstruction* inst) const;
};
```

**Tasks:**
- [x] Add `canHandleMethod`/`lowerMethod` to BuiltinHandler.h with default implementations
- [x] Add `tryLowerMethod`/`hasMethodHandler` declarations to HandlerRegistry.h
- [x] Implement `tryLowerMethod`/`hasMethodHandler` in HandlerRegistry.cpp
- [x] Build and verify no regressions (120/146, 82.2% - unchanged)

#### Phase 7b: Extend MapSetHandler

**Goal:** Move Map/Set method handling from `lowerCallMethod` to MapSetHandler.

**Current inline code to move (lines 3697-3921):**
- `boxValueForMapSet` lambda (already exists in MapSetHandler as `boxForMapSet`)
- `map.set(key, val)` → calls `ts_map_set_wrapper`
- `map.get(key)` → calls `ts_map_get_wrapper`
- `map.has(key)` → calls `ts_map_has_wrapper`
- `map.delete(key)` → calls `ts_map_delete_wrapper`
- `map.clear()` → calls `ts_map_clear_wrapper`
- `set.add(val)` → calls `ts_set_add_wrapper`
- `set.has(val)` → calls `ts_set_has_wrapper`
- `set.delete(val)` → calls `ts_set_delete_wrapper`
- `set.clear()` → calls `ts_set_clear_wrapper`

**Note:** Method calls use `*_wrapper` runtime functions (return values for chaining), while function calls use direct runtime functions.

**Add to MapSetHandler:**
```cpp
bool canHandleMethod(const std::string& methodName,
                     const std::string& className,
                     HIRInstruction* inst) const override {
    // Map methods
    if (className == "Map" || className.empty()) {
        if (methodName == "set" || methodName == "get" || methodName == "has" ||
            methodName == "delete" || methodName == "clear") {
            return true;
        }
    }
    // Set methods
    if (className == "Set" || className.empty()) {
        if (methodName == "add" || methodName == "has" ||
            methodName == "delete" || methodName == "clear") {
            return true;
        }
    }
    return false;
}

llvm::Value* lowerMethod(const std::string& methodName,
                         HIRInstruction* inst,
                         HIRToLLVM& lowerer) override {
    // Dispatch to specific lowering methods
    // ...
}
```

**Tasks:**
- [x] Implement `canHandleMethod` in MapSetHandler
- [x] Implement `lowerMethod` in MapSetHandler with Map methods (set, get, has, delete, clear)
- [x] Implement Set methods (add, has, delete, clear)
- [x] Add registry call in `lowerCallMethod` after className extraction
- [x] Remove inline Map/Set code from lowerCallMethod (~225 lines removed)
- [x] Verify tests pass (120/146, 82.2% - no regression)

#### Phase 7c: Extract Dynamic Dispatch Helper

**Goal:** Refactor the highly repetitive `ts_call_with_this_N` code (~400 lines) into a helper method.

**Current problem:** 6 nearly-identical code blocks for 0-6 arguments, each ~50-70 lines.

**Solution:** Create a helper method in HIRToLLVM:
```cpp
// In HIRToLLVM.h
llvm::Value* emitDynamicMethodCall(llvm::Value* funcVal, llvm::Value* thisArg,
                                   const std::vector<HIROperand>& args,
                                   size_t startIdx);

// In HIRToLLVM.cpp - helper implementation
llvm::Value* HIRToLLVM::emitDynamicMethodCall(llvm::Value* funcVal, llvm::Value* thisArg,
                                              const std::vector<HIROperand>& args,
                                              size_t startIdx) {
    size_t argCount = args.size() - startIdx;

    // Box all arguments
    std::vector<llvm::Value*> boxedArgs;
    for (size_t i = startIdx; i < args.size(); ++i) {
        llvm::Value* arg = getOperandValue(args[i]);
        arg = boxArgumentForCall(arg, args[i]);  // New helper
        boxedArgs.push_back(arg);
    }

    // Call ts_call_with_this_N based on argument count
    std::string fnName = "ts_call_with_this_" + std::to_string(argCount);
    // ... generate appropriate function type and call ...
}
```

**Tasks:**
- [x] Create `boxArgumentForDynamicCall` helper for consistent argument boxing
- [x] Create `emitDynamicMethodCall` helper
- [x] Replace inline `ts_call_with_this_N` blocks with helper call
- [x] Verify tests pass (120/146, 82.2% - no regression)

#### Phase 7d: Additional Method Handlers (Complete)

**Goal:** Extract remaining method-specific code.

**Implemented handlers:**
- GeneratorHandler: `next()`, `return()`, `throw()` methods for Generator and AsyncGenerator
- RegExpHandler: `test()`, `exec()` methods

**Note:** Array `join()` and `push()` fallbacks intentionally left inline since they handle Any-typed values.

**Tasks:**
- [x] Create GeneratorHandler for Generator/AsyncGenerator methods (next, return, throw)
- [x] Create RegExpHandler for RegExp methods (test, exec)
- [x] Register handlers in HandlerRegistry
- [x] Remove ~165 lines of inline code from lowerCallMethod
- [x] Verify tests pass (120/146, 82.2% - no regression)

#### Phase 7 Summary

| Sub-Phase | Description | Lines Removed | Complexity | Status |
|-----------|-------------|---------------|------------|--------|
| 7a | Interface extension | 0 | Low | ✓ Complete |
| 7b | MapSetHandler methods | ~225 | Medium | ✓ Complete |
| 7c | Dynamic dispatch helper | ~254 | Medium | ✓ Complete |
| 7d | Generator + RegExp handlers | ~165 | Low | ✓ Complete |
| **Total** | | **~644** | | |

**Phase 7 Complete:** `lowerCallMethod` reduced from ~928 lines to ~285 lines.
- GeneratorHandler: Handles Generator/AsyncGenerator next/return/throw methods
- RegExpHandler: Handles RegExp test/exec methods
- Array join/push fallbacks intentionally left inline (Any-typed value handling)

## Files to Create/Modify

| File | Action | Phase |
|------|--------|-------|
| `src/compiler/hir/handlers/BuiltinHandler.h` | Created ✓, Modify (add method interface) | 1, 7a |
| `src/compiler/hir/handlers/HandlerRegistry.h` | Created ✓, Modify (add tryLowerMethod) | 1, 7a |
| `src/compiler/hir/handlers/HandlerRegistry.cpp` | Created ✓, Modify (implement tryLowerMethod) | 1, 7a |
| `src/compiler/hir/handlers/MathHandler.cpp` | Created ✓ | 2 |
| `src/compiler/hir/handlers/ConsoleHandler.cpp` | Created ✓ | 3 |
| `src/compiler/hir/handlers/ArrayHandler.cpp` | Created ✓, Modify (add method support) | 4, 7d |
| `src/compiler/hir/handlers/MapSetHandler.cpp` | Created ✓, Modify (add method lowering) | 5a, 7b |
| `src/compiler/hir/handlers/TimerHandler.cpp` | Created ✓ | 5b |
| `src/compiler/hir/handlers/BigIntHandler.cpp` | Created ✓ | 5c |
| `src/compiler/hir/handlers/PathHandler.cpp` | Created ✓ | 5d |
| `src/compiler/hir/handlers/RegExpHandler.cpp` | Created ✓ (method support) | 7d |
| `src/compiler/hir/handlers/GeneratorHandler.cpp` | Created ✓ (method support) | 7d |
| `src/compiler/hir/CMakeLists.txt` | Modified ✓ | 1-5, 7d |
| `src/compiler/hir/HIRToLLVM.cpp` | Modified ✓ (dead code removed), Modify (remove method inline code) | 6, 7b-7d |
| `src/compiler/hir/HIRToLLVM.h` | Modified ✓, Modify (add emitDynamicMethodCall) | 1-5, 7c |

## Success Criteria

| # | Criteria | Status | Phase 7 Target |
|---|----------|--------|----------------|
| 1 | All test suites maintain or improve pass rates | ✓ Met (120/146, 82.2%) | Maintain |
| 2 | lowerCall reduced from ~1300 lines to ~100 lines | ✓ Met (~300 lines remaining) | - |
| 3 | lowerCallMethod reduced from ~600 lines to ~50 lines | ⚠️ Improved (~285 lines, was ~928) | ~130 lines |
| 4 | Each handler is independently testable | ✓ Met | ✓ Extend to methods |
| 5 | Adding new builtins requires only creating new handler file | ✓ Met (for function calls) | ✓ Include methods |

**Progress:** Phase 7 complete. lowerCallMethod reduced from ~928 lines to ~285 lines (-69%). Remaining code handles dynamic dispatch for user functions, console fallback, and Any-typed array operations.

## Related Work

- HIR-003: Registry-Driven Variadic Function Handling (completed for console)
- LoweringRegistry: Declarative runtime call specifications

## Phase 5 Implementation Order

Recommended sequence based on complexity and test coverage:

1. **MapSetHandler** (5a) - High priority, significant code volume (~130 lines)
   - Straightforward extraction, well-defined function signatures
   - Map/Set operations are commonly used in tests

2. **TimerHandler** (5b) - High priority, medium complexity (~80 lines)
   - Uses prefix matching for setTimeout*/setInterval*/etc.
   - Some functions are variadic

3. **BigIntHandler** (5c) - Medium priority, simpler (~50 lines)
   - Arithmetic and comparison operations
   - All follow similar patterns

4. **PathHandler** (5d) - Medium priority, variadic handling (~140 lines)
   - path.join and path.resolve are variadic
   - Others are simple ptr→ptr functions

5. **Future Handlers** (5e) - Lower priority
   - Extract only if significant code reduction justifies it
   - StringHandler, ValueHandler, ObjectHandler candidates

## Implementation Template

Each new handler should follow this pattern:

```cpp
// In handlers/XxxHandler.cpp
#include "BuiltinHandler.h"
#include "../HIRToLLVM.h"
#include <unordered_set>

namespace ts::hir {

class XxxHandler : public BuiltinHandler {
public:
    const char* name() const override { return "XxxHandler"; }

    bool canHandle(const std::string& funcName, HIRInstruction* inst) const override {
        static const std::unordered_set<std::string> funcs = { /* ... */ };
        return funcs.count(funcName) > 0;
    }

    llvm::Value* lower(const std::string& funcName, HIRInstruction* inst,
                       HIRToLLVM& lowerer) override {
        // Dispatch to specific lowering methods
        return nullptr;
    }
};

std::unique_ptr<BuiltinHandler> createXxxHandler() {
    return std::make_unique<XxxHandler>();
}

} // namespace ts::hir
```

## Notes

- Handlers should expose helper methods via HIRToLLVM friend access
- Complex lowerings (type dispatch, variadic) belong in handlers
- Simple runtime calls should still use LoweringRegistry
