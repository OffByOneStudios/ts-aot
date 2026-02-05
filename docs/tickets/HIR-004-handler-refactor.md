# HIR-004: Handler Registration Pattern for Builtin Functions

**Status:** In Progress
**Category:** Architecture / Refactoring
**Priority:** High

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
- [ ] MapSetHandler (Map, Set, WeakMap, WeakSet)
- [ ] PathHandler (path.join, path.resolve, etc.)
- [ ] TimerHandler (setTimeout, setInterval)
- [ ] BigIntHandler
- [ ] GeneratorHandler
- [ ] RegExpHandler

### Phase 6: Cleanup
- [ ] Remove empty cases from lowerCall/lowerCallMethod
- [ ] Update documentation
- [ ] Final verification of all test suites

## Files to Create/Modify

| File | Action |
|------|--------|
| `src/compiler/hir/handlers/BuiltinHandler.h` | Created ✓ |
| `src/compiler/hir/handlers/HandlerRegistry.h` | Created ✓ |
| `src/compiler/hir/handlers/HandlerRegistry.cpp` | Created ✓ |
| `src/compiler/hir/handlers/MathHandler.cpp` | Created ✓ (Phase 2) |
| `src/compiler/hir/handlers/ConsoleHandler.cpp` | Created ✓ (Phase 3) |
| `src/compiler/hir/handlers/ArrayHandler.cpp` | Created ✓ (Phase 4) |
| `src/compiler/hir/CMakeLists.txt` | Modify (add handlers/) |
| `src/compiler/hir/HIRToLLVM.cpp` | Modify (use registry) |
| `src/compiler/hir/HIRToLLVM.h` | Modify (add friend class) |

## Success Criteria

1. All test suites maintain or improve pass rates
2. lowerCall reduced from ~1300 lines to ~100 lines
3. lowerCallMethod reduced from ~600 lines to ~50 lines
4. Each handler is independently testable
5. Adding new builtins requires only creating new handler file

## Related Work

- HIR-003: Registry-Driven Variadic Function Handling (completed for console)
- LoweringRegistry: Declarative runtime call specifications

## Notes

- Handlers should expose helper methods via HIRToLLVM friend access
- Complex lowerings (type dispatch, variadic) belong in handlers
- Simple runtime calls should still use LoweringRegistry
