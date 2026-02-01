# REFACTOR-001: HIR Lowering Registry

**Status:** Complete
**Completed:** 2026-01-31
**Category:** Compiler Architecture
**Priority:** Medium
**Estimated Effort:** Large (3-5 days)

## Implementation Progress

### Phase 1: Infrastructure ✅ COMPLETE

**Files Created:**
- `src/compiler/hir/LoweringRegistry.h` - Core data structures and builder pattern
- `src/compiler/hir/LoweringRegistry.cpp` - 100+ builtin registrations

**Files Modified:**
- `src/compiler/hir/HIRToLLVM.h` - Added `lowerRegisteredCall()`, `convertArg()`, `handleReturn()` declarations
- `src/compiler/hir/HIRToLLVM.cpp` - Added registry lookup in `lowerCall()` and implementation of helper methods
- `src/compiler/CMakeLists.txt` - Added `hir/LoweringRegistry.cpp` to build

**Builtins Registered (100+):**
- Math operations: abs, sqrt, floor, ceil, round, pow, min, max, sin, cos, tan, log, exp, etc.
- Array methods: push, pop, shift, unshift, get, set, length, join, slice, concat, includes, indexOf, etc.
- String operations: create, concat, length, at, charAt, charCodeAt, includes, indexOf, etc.
- Map/Set methods: create, get, set, has, delete, size, keys, values, entries, forEach, clear
- JSON operations: parse, stringify
- Object operations: keys, values, entries, create, freeze, seal
- BigInt operations: add, sub, mul, div, mod, neg
- Symbol operations: create, for, keyFor, description
- Error operations: create
- Type operations: typeof, instanceof

**Verified Working:**
- Golden HIR tests pass with registry-based lowering (79.5% pass rate - same as before)
- `array_access.ts` and `call_to_llvm.ts` tests pass, confirming registry integration works

### Remaining Work

**Console Functions (Intentionally Inline):**
Console functions need type-specific dispatch (e.g., `ts_console_log_int`, `ts_console_log_double`, `ts_console_log_string`) based on the argument type at compile time. This cannot be captured in the current declarative registry format which assumes a fixed runtime function name.

Options for future:
1. Add type-based dispatch to registry (adds complexity)
2. Keep console functions inline (current approach - simpler)

**Other Inline Checks Still Present:**
- User-defined function calls (must remain inline - looked up from function table)
- Variadic function handling for some builtins
- Complex multi-step lowerings that can't be expressed declaratively

---

## Problem Statement

`HIRToLLVM.cpp` contains 90+ inline string checks in `lowerCall()` and `lowerCallMethod()` to handle different runtime functions. This creates:

1. **Maintainability Issues** - Adding a new builtin requires finding the right location and copying boilerplate
2. **Code Duplication** - Many builtins follow similar patterns but are copy-pasted
3. **Inconsistency** - Different builtins handle boxing/unboxing differently
4. **Testing Difficulty** - Hard to verify all builtins are handled correctly

### Current State Example

```cpp
// In lowerCall() - 900+ lines of this pattern
if (funcName == "ts_console_log") {
    auto ft = llvm::FunctionType::get(voidTy, {ptrTy}, false);
    auto fn = getOrDeclareFunction("ts_console_log", ft);
    llvm::Value* boxed = boxValue(args[0], ...);
    builder_.CreateCall(ft, fn, {boxed});
    return llvm::ConstantPointerNull::get(ptrTy);
}
if (funcName == "ts_array_push") {
    // Similar pattern...
}
// ... 90+ more
```

## Proposed Solution: LoweringRegistry

Create a `LoweringRegistry` that maps HIR call targets to their LLVM lowering specifications.

### Design Goals

1. **Declarative** - Define builtins declaratively, not imperatively
2. **Type-Safe** - Capture LLVM types, boxing requirements, and conversions
3. **Extensible** - Easy to add new builtins
4. **Testable** - Registry can be validated independently
5. **Consistent** - All builtins use the same lowering path

## Data Structures

### LoweringSpec

```cpp
// src/compiler/hir/LoweringRegistry.h

namespace hir {

/// How to convert an HIR value to LLVM IR
enum class ArgConversion {
    None,           // Pass through as-is
    Box,            // Box to TsValue*
    Unbox,          // Unbox from TsValue*
    ToI64,          // Convert to i64
    ToF64,          // Convert to f64
    ToI32,          // Convert to i32
    ToBool,         // Convert to i1
    PtrToInt,       // Convert ptr to i64
    IntToPtr,       // Convert i64 to ptr
};

/// How to handle the return value
enum class ReturnHandling {
    Void,           // No return value
    Raw,            // Return as-is
    Boxed,          // Return is boxed TsValue*
    BoxResult,      // Box the raw result before returning
};

/// Specification for lowering a single runtime call
struct LoweringSpec {
    std::string runtimeFuncName;        // e.g., "ts_console_log"

    // LLVM return type (nullptr for void)
    std::function<llvm::Type*(llvm::LLVMContext&)> returnType;

    // LLVM argument types
    std::vector<std::function<llvm::Type*(llvm::LLVMContext&)>> argTypes;

    // How to convert each argument
    std::vector<ArgConversion> argConversions;

    // How to handle return value
    ReturnHandling returnHandling = ReturnHandling::Raw;

    // Whether function is variadic
    bool isVariadic = false;

    // Optional custom lowering (for complex cases)
    std::function<llvm::Value*(HIRToLLVM&, CallInst*,
                               const std::vector<llvm::Value*>&)> customLowering;
};

} // namespace hir
```

### LoweringRegistry

```cpp
// src/compiler/hir/LoweringRegistry.h

namespace hir {

class LoweringRegistry {
public:
    static LoweringRegistry& instance();

    /// Register a lowering spec for a runtime function
    void registerLowering(const std::string& hirFuncName, LoweringSpec spec);

    /// Look up lowering spec (returns nullptr if not found)
    const LoweringSpec* lookup(const std::string& hirFuncName) const;

    /// Check if a function has a registered lowering
    bool hasLowering(const std::string& hirFuncName) const;

    /// Initialize with all builtin lowerings
    static void registerBuiltins();

private:
    LoweringRegistry() = default;
    std::unordered_map<std::string, LoweringSpec> registry_;
};

} // namespace hir
```

### Builder Pattern for Registration

```cpp
// src/compiler/hir/LoweringRegistry.h

namespace hir {

class LoweringSpecBuilder {
public:
    explicit LoweringSpecBuilder(const std::string& runtimeFunc)
        : spec_{.runtimeFuncName = runtimeFunc} {}

    LoweringSpecBuilder& returns(std::function<llvm::Type*(llvm::LLVMContext&)> type) {
        spec_.returnType = std::move(type);
        return *this;
    }

    LoweringSpecBuilder& returnsVoid() {
        spec_.returnType = nullptr;
        spec_.returnHandling = ReturnHandling::Void;
        return *this;
    }

    LoweringSpecBuilder& returnsPtr() {
        spec_.returnType = [](auto& ctx) { return llvm::PointerType::get(ctx, 0); };
        return *this;
    }

    LoweringSpecBuilder& returnsI64() {
        spec_.returnType = [](auto& ctx) { return llvm::Type::getInt64Ty(ctx); };
        return *this;
    }

    LoweringSpecBuilder& returnsBoxed() {
        spec_.returnType = [](auto& ctx) { return llvm::PointerType::get(ctx, 0); };
        spec_.returnHandling = ReturnHandling::Boxed;
        return *this;
    }

    LoweringSpecBuilder& arg(std::function<llvm::Type*(llvm::LLVMContext&)> type,
                             ArgConversion conv = ArgConversion::None) {
        spec_.argTypes.push_back(std::move(type));
        spec_.argConversions.push_back(conv);
        return *this;
    }

    LoweringSpecBuilder& ptrArg(ArgConversion conv = ArgConversion::None) {
        return arg([](auto& ctx) { return llvm::PointerType::get(ctx, 0); }, conv);
    }

    LoweringSpecBuilder& boxedArg() {
        return ptrArg(ArgConversion::Box);
    }

    LoweringSpecBuilder& i64Arg(ArgConversion conv = ArgConversion::None) {
        return arg([](auto& ctx) { return llvm::Type::getInt64Ty(ctx); }, conv);
    }

    LoweringSpecBuilder& variadic() {
        spec_.isVariadic = true;
        return *this;
    }

    LoweringSpecBuilder& custom(
        std::function<llvm::Value*(HIRToLLVM&, CallInst*,
                                   const std::vector<llvm::Value*>&)> fn) {
        spec_.customLowering = std::move(fn);
        return *this;
    }

    LoweringSpec build() { return std::move(spec_); }

private:
    LoweringSpec spec_;
};

// Convenience function
inline LoweringSpecBuilder lowering(const std::string& runtimeFunc) {
    return LoweringSpecBuilder(runtimeFunc);
}

} // namespace hir
```

## Registration Example

```cpp
// src/compiler/hir/LoweringRegistry.cpp

void LoweringRegistry::registerBuiltins() {
    auto& reg = instance();

    // Console functions
    reg.registerLowering("ts_console_log",
        lowering("ts_console_log")
            .returnsVoid()
            .boxedArg()
            .build());

    reg.registerLowering("ts_console_error",
        lowering("ts_console_error")
            .returnsVoid()
            .boxedArg()
            .build());

    // Math functions
    reg.registerLowering("ts_math_abs",
        lowering("ts_math_abs")
            .returnsI64()
            .i64Arg()
            .build());

    reg.registerLowering("ts_math_sqrt",
        lowering("sqrt")  // Use libm directly
            .returns([](auto& ctx) { return llvm::Type::getDoubleTy(ctx); })
            .arg([](auto& ctx) { return llvm::Type::getDoubleTy(ctx); },
                 ArgConversion::ToF64)
            .build());

    // Array methods
    reg.registerLowering("ts_array_push",
        lowering("ts_array_push")
            .returnsI64()
            .ptrArg()      // array
            .boxedArg()    // value
            .build());

    reg.registerLowering("ts_array_get",
        lowering("ts_array_get")
            .returnsBoxed()
            .ptrArg()      // array
            .i64Arg()      // index
            .build());

    // Map/Set methods with custom lowering for complex cases
    reg.registerLowering("ts_map_set",
        lowering("ts_map_set")
            .returnsPtr()
            .ptrArg()      // map
            .boxedArg()    // key
            .boxedArg()    // value
            .custom([](HIRToLLVM& gen, CallInst* call,
                       const std::vector<llvm::Value*>& args) {
                // Return the map itself for chaining
                return args[0];
            })
            .build());
}
```

## Integration with HIRToLLVM

### Modified lowerCall()

```cpp
// src/compiler/hir/HIRToLLVM.cpp

llvm::Value* HIRToLLVM::lowerCall(CallInst* call) {
    const std::string& funcName = call->callee;

    // Try registry first
    if (auto* spec = LoweringRegistry::instance().lookup(funcName)) {
        return lowerRegisteredCall(call, *spec);
    }

    // Fall back to existing inline handling for:
    // 1. User-defined functions
    // 2. Not-yet-migrated builtins (during transition)
    // 3. Complex cases requiring inline logic

    // ... existing inline code ...
}

llvm::Value* HIRToLLVM::lowerRegisteredCall(CallInst* call,
                                             const LoweringSpec& spec) {
    auto& ctx = module_.getContext();

    // Build LLVM function type
    llvm::Type* retTy = spec.returnType
        ? spec.returnType(ctx)
        : llvm::Type::getVoidTy(ctx);

    std::vector<llvm::Type*> argTys;
    for (const auto& argType : spec.argTypes) {
        argTys.push_back(argType(ctx));
    }

    auto* ft = llvm::FunctionType::get(retTy, argTys, spec.isVariadic);
    auto fn = getOrDeclareFunction(spec.runtimeFuncName, ft);

    // Convert arguments
    std::vector<llvm::Value*> llvmArgs;
    for (size_t i = 0; i < call->args.size() && i < spec.argConversions.size(); ++i) {
        llvm::Value* arg = valueMap_.at(call->args[i]);
        arg = convertArg(arg, spec.argConversions[i]);
        llvmArgs.push_back(arg);
    }

    // Handle custom lowering
    if (spec.customLowering) {
        return spec.customLowering(*this, call, llvmArgs);
    }

    // Standard call
    llvm::Value* result = builder_.CreateCall(ft, fn, llvmArgs);

    // Handle return
    return handleReturn(result, spec.returnHandling);
}

llvm::Value* HIRToLLVM::convertArg(llvm::Value* arg, ArgConversion conv) {
    switch (conv) {
        case ArgConversion::None:
            return arg;
        case ArgConversion::Box:
            return boxValue(arg);
        case ArgConversion::Unbox:
            return unboxValue(arg);
        case ArgConversion::ToI64:
            return builder_.CreateFPToSI(arg, builder_.getInt64Ty());
        case ArgConversion::ToF64:
            return builder_.CreateSIToFP(arg, builder_.getDoubleTy());
        case ArgConversion::ToI32:
            return builder_.CreateTrunc(arg, builder_.getInt32Ty());
        case ArgConversion::ToBool:
            return builder_.CreateICmpNE(arg,
                llvm::ConstantInt::get(arg->getType(), 0));
        case ArgConversion::PtrToInt:
            return builder_.CreatePtrToInt(arg, builder_.getInt64Ty());
        case ArgConversion::IntToPtr:
            return builder_.CreateIntToPtr(arg, builder_.getPtrTy());
    }
    return arg;
}

llvm::Value* HIRToLLVM::handleReturn(llvm::Value* result, ReturnHandling handling) {
    switch (handling) {
        case ReturnHandling::Void:
            return llvm::ConstantPointerNull::get(builder_.getPtrTy());
        case ReturnHandling::Raw:
            return result;
        case ReturnHandling::Boxed:
            markAsBoxed(result);
            return result;
        case ReturnHandling::BoxResult:
            return boxValue(result);
    }
    return result;
}
```

## Migration Strategy

### Phase 1: Infrastructure (Day 1)

1. Create `LoweringRegistry.h` and `LoweringRegistry.cpp`
2. Implement `LoweringSpec`, `LoweringSpecBuilder`, and `LoweringRegistry`
3. Add `lowerRegisteredCall()` to `HIRToLLVM`
4. Add registry initialization to compiler startup

### Phase 2: Console Functions (Day 1-2)

Migrate first batch of simple functions:

| Function | Args | Return |
|----------|------|--------|
| `ts_console_log` | 1 boxed | void |
| `ts_console_error` | 1 boxed | void |
| `ts_console_warn` | 1 boxed | void |
| `ts_console_info` | 1 boxed | void |
| `ts_console_debug` | 1 boxed | void |
| `ts_console_dir` | 1 boxed | void |
| `ts_console_table` | 1 boxed | void |
| `ts_console_assert` | 2 boxed | void |
| `ts_console_count` | 1 ptr | void |
| `ts_console_count_reset` | 1 ptr | void |
| `ts_console_time` | 1 ptr | void |
| `ts_console_time_end` | 1 ptr | void |
| `ts_console_time_log` | 2 args | void |
| `ts_console_group` | 1 ptr | void |
| `ts_console_group_collapsed` | 1 ptr | void |
| `ts_console_group_end` | 0 | void |

### Phase 3: Math Functions (Day 2)

| Function | Args | Return | Notes |
|----------|------|--------|-------|
| `ts_math_abs` | i64 | i64 | Direct |
| `ts_math_abs_f64` | f64 | f64 | Or use fabs |
| `sqrt` | f64 | f64 | libm |
| `floor` | f64 | f64 | libm |
| `ceil` | f64 | f64 | libm |
| `round` | f64 | f64 | libm |
| `sin`/`cos`/`tan` | f64 | f64 | libm |
| `pow` | f64, f64 | f64 | libm |
| `ts_math_random` | 0 | f64 | Custom |
| `ts_math_min` | variadic | f64 | Custom |
| `ts_math_max` | variadic | f64 | Custom |

### Phase 4: Array Methods (Day 2-3)

| Function | Args | Return | Notes |
|----------|------|--------|-------|
| `ts_array_create` | 0 | ptr | |
| `ts_array_push` | ptr, boxed | i64 | Returns new length |
| `ts_array_pop` | ptr | boxed | |
| `ts_array_shift` | ptr | boxed | |
| `ts_array_unshift` | ptr, boxed | i64 | |
| `ts_array_get` | ptr, i64 | boxed | |
| `ts_array_set` | ptr, i64, boxed | void | |
| `ts_array_length` | ptr | i64 | |
| `ts_array_join` | ptr, ptr | ptr | |
| `ts_array_slice` | ptr, i64, i64 | ptr | |
| `ts_array_concat` | ptr, ptr | ptr | |
| `ts_array_includes` | ptr, boxed | i1 | |
| `ts_array_indexOf` | ptr, boxed | i64 | |
| ... and 20+ more | | | |

### Phase 5: Map/Set Methods (Day 3-4)

| Function | Pattern |
|----------|---------|
| `ts_map_create` | 0 → ptr |
| `ts_map_get` | ptr, boxed → boxed |
| `ts_map_set` | ptr, boxed, boxed → ptr (chaining) |
| `ts_map_has` | ptr, boxed → i1 |
| `ts_map_delete` | ptr, boxed → i1 |
| `ts_map_size` | ptr → i64 |
| `ts_map_keys` | ptr → ptr |
| `ts_map_values` | ptr → ptr |
| `ts_map_entries` | ptr → ptr |
| `ts_map_forEach` | ptr, ptr → void |
| `ts_set_*` | Similar pattern | |
| `ts_weakmap_*` | Similar pattern | |
| `ts_weakset_*` | Similar pattern | |

### Phase 6: Remaining Builtins (Day 4-5)

- BigInt operations (6 functions)
- Generator operations (6 functions)
- String operations
- Object operations
- Promise operations
- Buffer operations
- Regex operations
- Date operations
- JSON operations
- TypedArray operations

### Phase 7: Cleanup (Day 5)

1. Remove migrated inline code from `lowerCall()`
2. Update tests
3. Document registry API
4. Add validation for duplicate registrations

## Testing Strategy

### Unit Tests

```cpp
// tests/hir/LoweringRegistryTest.cpp

TEST(LoweringRegistry, ConsoleLogSpec) {
    auto* spec = LoweringRegistry::instance().lookup("ts_console_log");
    ASSERT_NE(spec, nullptr);
    EXPECT_EQ(spec->runtimeFuncName, "ts_console_log");
    EXPECT_EQ(spec->argConversions.size(), 1);
    EXPECT_EQ(spec->argConversions[0], ArgConversion::Box);
    EXPECT_EQ(spec->returnHandling, ReturnHandling::Void);
}

TEST(LoweringRegistry, AllBuiltinsRegistered) {
    auto& reg = LoweringRegistry::instance();
    // Verify all expected builtins are registered
    EXPECT_TRUE(reg.hasLowering("ts_console_log"));
    EXPECT_TRUE(reg.hasLowering("ts_array_push"));
    EXPECT_TRUE(reg.hasLowering("ts_map_get"));
    // ... etc
}
```

### Integration Tests

Run existing golden HIR tests to verify no regressions:

```bash
python tests/golden_ir/runner.py tests/golden_hir
```

## Success Criteria

1. **All 90+ inline checks migrated** to registry-based lowering
2. **No test regressions** in golden HIR test suite
3. **Reduced code duplication** in `HIRToLLVM.cpp`
4. **Easier to add new builtins** - just add a registry entry
5. **Better documentation** of builtin signatures

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Custom lowering cases | Keep `customLowering` escape hatch |
| Performance regression | Benchmark before/after |
| Missing edge cases | Comprehensive test coverage |
| Large refactor scope | Phased migration approach |

## Dependencies

- None (self-contained refactor)

## Related Work

- `BuiltinRegistry` - HIR-level method resolution (pre-existing)
- `MethodResolutionPass` - Resolves method calls in HIR
- `BuiltinResolutionPass` - Resolves global builtin calls in HIR

## Future Extensions

1. **Code generation validation** - Verify generated LLVM IR matches spec
2. **Auto-documentation** - Generate API docs from registry
3. **Performance hints** - Add inlining hints, pure function markers
4. **Cross-platform handling** - Different lowerings per target platform
