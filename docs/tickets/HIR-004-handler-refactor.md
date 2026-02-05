# HIR-004: Handler Registration Pattern for Builtin Functions

**Status:** Phase 5 Complete, Phase 6 Ready
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
- [ ] GeneratorHandler - Generator functions (if present)
- [ ] RegExpHandler - RegExp operations (if present)
- [ ] StringHandler - String methods not in LoweringRegistry
- [ ] ValueHandler - ts_value_* boxing operations
- [ ] ObjectHandler - ts_object_* operations

### Phase 6: Cleanup (Ready to Execute)

The HandlerRegistry check (line 2733) now catches handled functions BEFORE inline code.
Dead inline code in `lowerCall` that can be safely removed:

| Handler | Lines | Dead Code Size |
|---------|-------|----------------|
| ConsoleHandler | 2741-2782 | ~42 lines |
| ArrayHandler | 2909-2970, 3436-3544 | ~175 lines |
| MathHandler | 2993-3113, 3197-3336 | ~260 lines |
| TimerHandler | 3115-3195 | ~81 lines |
| BigIntHandler | 3386-3434 | ~49 lines |
| MapSetHandler | 3546-3826 | ~281 lines |
| PathHandler | 3828-3963 | ~136 lines |
| **Total** | | **~1024 lines** |

Code that must REMAIN in `lowerCall`:
- ts_value_to_bool, ts_value_is_undefined, ts_value_is_nullish (2784-2821)
- ts_object_has_property (2823-2835)
- ts_string_from_* functions (2837-2906)
- ts_object_assign (2972-2991)
- ts_object_is (3338-3384)
- ts_json_stringify (3965-4009)
- Generic function call handling (4020-4043)

Tasks:
- [ ] Remove dead handler inline code (~1024 lines)
- [ ] Update documentation
- [ ] Final verification of all test suites

## Files to Create/Modify

| File | Action | Phase |
|------|--------|-------|
| `src/compiler/hir/handlers/BuiltinHandler.h` | Created ✓ | 1 |
| `src/compiler/hir/handlers/HandlerRegistry.h` | Created ✓ | 1 |
| `src/compiler/hir/handlers/HandlerRegistry.cpp` | Created ✓ | 1 |
| `src/compiler/hir/handlers/MathHandler.cpp` | Created ✓ | 2 |
| `src/compiler/hir/handlers/ConsoleHandler.cpp` | Created ✓ | 3 |
| `src/compiler/hir/handlers/ArrayHandler.cpp` | Created ✓ | 4 |
| `src/compiler/hir/handlers/MapSetHandler.cpp` | Created ✓ | 5a |
| `src/compiler/hir/handlers/TimerHandler.cpp` | Created ✓ | 5b |
| `src/compiler/hir/handlers/BigIntHandler.cpp` | Created ✓ | 5c |
| `src/compiler/hir/handlers/PathHandler.cpp` | Created ✓ | 5d |
| `src/compiler/hir/CMakeLists.txt` | Modified ✓ | 1-5 |
| `src/compiler/hir/HIRToLLVM.cpp` | Modify (use registry) | 6 |
| `src/compiler/hir/HIRToLLVM.h` | Modified ✓ (add friend classes) | 1-5 |

## Success Criteria

1. All test suites maintain or improve pass rates
2. lowerCall reduced from ~1300 lines to ~100 lines
3. lowerCallMethod reduced from ~600 lines to ~50 lines
4. Each handler is independently testable
5. Adding new builtins requires only creating new handler file

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
