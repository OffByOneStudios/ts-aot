# HIR Pass Infrastructure Design

## Overview

This document describes the design for a proper HIR pass infrastructure for ts-aot.
The goal is to transform high-level HIR operations into lower-level forms through
a series of well-defined passes, similar to how LLVM and other production compilers work.

## Current State

The HIR pipeline is currently:
```
AST → ASTToHIR → HIRModule → HIRToLLVM → LLVM IR
```

No optimization or analysis passes exist between ASTToHIR and HIRToLLVM.

### Problem Example: Array Methods

```typescript
let arr: number[] = [1, 2, 3];
arr.push(4);  // Currently generates CallMethod, not ArrayPush
```

ASTToHIR generates:
```
%0 = call_method %arr, "push", (%val)
```

But HIR has a dedicated `ArrayPush` opcode that HIRToLLVM knows how to lower:
```
%0 = array.push %arr, %val
```

The missing piece is a pass that transforms CallMethod → ArrayPush when applicable.

## Design Goals

1. **Separation of concerns** - Each pass does one thing well
2. **Testability** - Passes can be unit tested in isolation
3. **Composability** - Passes can be combined in different orders
4. **Debuggability** - Easy to dump HIR between passes
5. **Extensibility** - Adding new passes should be straightforward

## Pass Infrastructure

### Base Pass Class

```cpp
namespace ts::hir {

// Result of running a pass
struct PassResult {
    bool changed;           // Did the pass modify the IR?
    std::string error;      // Empty if successful

    static PassResult unchanged() { return {false, ""}; }
    static PassResult modified() { return {true, ""}; }
    static PassResult failed(const std::string& msg) { return {false, msg}; }
};

// Base class for all HIR passes
class HIRPass {
public:
    virtual ~HIRPass() = default;

    // Human-readable name for debugging
    virtual const char* name() const = 0;

    // Run on entire module
    virtual PassResult run(HIRModule& module) = 0;

    // Optional: run on single function (for function passes)
    virtual PassResult runOnFunction(HIRFunction& func) {
        return PassResult::unchanged();
    }
};

// Pass that operates on one function at a time
class HIRFunctionPass : public HIRPass {
public:
    PassResult run(HIRModule& module) override {
        PassResult result = PassResult::unchanged();
        for (auto& func : module.functions) {
            auto funcResult = runOnFunction(*func);
            if (!funcResult.error.empty()) return funcResult;
            if (funcResult.changed) result.changed = true;
        }
        return result;
    }

    virtual PassResult runOnFunction(HIRFunction& func) override = 0;
};

} // namespace ts::hir
```

### Pass Manager

```cpp
namespace ts::hir {

class PassManager {
public:
    // Add a pass to the pipeline
    void addPass(std::unique_ptr<HIRPass> pass);

    // Run all passes in order
    PassResult run(HIRModule& module);

    // Enable/disable debug output between passes
    void setDebugPrinting(bool enabled);

    // Set output stream for debug printing
    void setDebugStream(std::ostream& os);

private:
    std::vector<std::unique_ptr<HIRPass>> passes_;
    bool debugPrinting_ = false;
    std::ostream* debugStream_ = &std::cerr;
};

} // namespace ts::hir
```

## Required Passes

### Phase 1: Core Functionality

These passes are needed to get basic programs working:

#### 1. MethodResolutionPass

Transforms high-level method calls to specific operations when type is known.

**Input:**
```
%0 = call_method %arr, "push", (%val)    ; arr is Array type
```

**Output:**
```
%0 = array.push %arr, %val
```

**Transformations:**
| Type | Method | HIR Opcode |
|------|--------|------------|
| Array | push | ArrayPush |
| Array | pop | ArrayPop (new) |
| Array | length | ArrayLength |
| Array | [n] | GetElem / GetElemTyped |
| String | length | StringLength (new) |
| String | charAt | StringCharAt (new) |
| String | concat / + | StringConcat |
| Object | keys | ObjectKeys (new) |

For unknown types or unrecognized methods, leave as CallMethod for runtime dispatch.

#### 2. BuiltinResolutionPass

Transforms calls to known runtime functions.

**Input:**
```
%0 = call_method %console, "log", (%msg)
```

**Output:**
```
call @ts_console_log(%msg)
```

**Scope:** console.*, Math.*, JSON.*, etc.

#### 3. TypePropagationPass

Ensures all values have accurate type information by propagating types through
the SSA graph. This enables other passes to make informed decisions.

**Example:**
```
%arr = new_array.boxed 3        ; type: Array<Any>
%elem = get_elem %arr, 0        ; type: Any (propagated from array element type)
```

### Phase 2: Optimization

#### 4. ConstantFoldingPass

Evaluates constant expressions at compile time.

**Input:**
```
%a = const.i64 5
%b = const.i64 3
%c = add.i64 %a, %b
```

**Output:**
```
%c = const.i64 8
```

#### 5. DeadCodeEliminationPass

Removes instructions whose results are never used.

#### 6. CommonSubexpressionEliminationPass

Identifies and reuses identical computations.

### Phase 3: Advanced (Future)

- **InliningPass** - Inline small functions
- **EscapeAnalysisPass** - Determine if objects escape (for stack allocation)
- **LoopOptimizationPass** - Strength reduction, hoisting
- **DevirtualizationPass** - Convert virtual calls to direct when possible

## Pass Ordering

The standard pass pipeline:

```cpp
PassManager pm;

// Phase 1: Resolution (required for correctness)
pm.addPass(std::make_unique<TypePropagationPass>());
pm.addPass(std::make_unique<MethodResolutionPass>());
pm.addPass(std::make_unique<BuiltinResolutionPass>());

// Phase 2: Optimization (optional, for release builds)
if (optimizationLevel > 0) {
    pm.addPass(std::make_unique<ConstantFoldingPass>());
    pm.addPass(std::make_unique<DeadCodeEliminationPass>());
    pm.addPass(std::make_unique<CommonSubexpressionEliminationPass>());
}

pm.run(module);
```

## Analysis Infrastructure

Some passes need analysis results (e.g., dominance, liveness). We'll add analysis
passes that compute and cache results.

```cpp
// Analysis result cache
class AnalysisManager {
public:
    template<typename AnalysisT>
    typename AnalysisT::Result& getAnalysis(HIRFunction& func);

    void invalidate(HIRFunction& func);

private:
    // Cache of analysis results per function
    std::unordered_map<HIRFunction*, std::any> cache_;
};

// Example analysis
class DominanceAnalysis {
public:
    struct Result {
        std::map<HIRBlock*, HIRBlock*> idom;  // Immediate dominators
        std::map<HIRBlock*, std::set<HIRBlock*>> domTree;
    };

    static Result run(HIRFunction& func);
};
```

## File Organization

```
src/compiler/hir/
├── HIR.h                    # Data structures
├── HIR.cpp
├── HIRBuilder.h             # Construction API
├── ASTToHIR.h               # AST → HIR lowering
├── ASTToHIR.cpp
├── HIRToLLVM.h              # HIR → LLVM lowering
├── HIRToLLVM.cpp
├── HIRPrinter.h             # Debug output
├── HIRPrinter.cpp
├── passes/                  # NEW: Pass infrastructure
│   ├── HIRPass.h            # Base class
│   ├── PassManager.h
│   ├── PassManager.cpp
│   ├── MethodResolutionPass.h
│   ├── MethodResolutionPass.cpp
│   ├── BuiltinResolutionPass.h
│   ├── BuiltinResolutionPass.cpp
│   ├── TypePropagationPass.h
│   ├── TypePropagationPass.cpp
│   ├── ConstantFoldingPass.h
│   ├── ConstantFoldingPass.cpp
│   └── ...
└── analysis/                # NEW: Analysis infrastructure
    ├── AnalysisManager.h
    ├── DominanceAnalysis.h
    ├── DominanceAnalysis.cpp
    └── ...
```

## Integration with Driver

The compiler driver will be updated to run the pass pipeline:

```cpp
// In Driver.cpp
HIRModule module = ASTToHIR::lower(ast);

PassManager pm;
pm.addPass(std::make_unique<TypePropagationPass>());
pm.addPass(std::make_unique<MethodResolutionPass>());
pm.addPass(std::make_unique<BuiltinResolutionPass>());

if (options.dumpHIRBeforePasses) {
    HIRPrinter::print(module, std::cerr);
}

pm.run(module);

if (options.dumpHIRAfterPasses) {
    HIRPrinter::print(module, std::cerr);
}

llvm::Module* llvmModule = HIRToLLVM::lower(module);
```

## New Opcodes Needed

To support method resolution, we need additional HIR opcodes:

### Array Operations (extend existing)
- `ArrayPop` - Pop element from array
- `ArrayShift` - Remove first element
- `ArrayUnshift` - Add element at beginning
- `ArraySlice` - Create slice
- `ArrayConcat` - Concatenate arrays
- `ArrayIndexOf` - Find element index
- `ArrayIncludes` - Check if contains element
- `ArrayJoin` - Join to string
- `ArrayReverse` - Reverse in place
- `ArraySort` - Sort array

### String Operations (new)
- `StringLength` - Get string length
- `StringCharAt` - Get character at index
- `StringCharCodeAt` - Get char code at index
- `StringSubstring` - Extract substring
- `StringIndexOf` - Find substring
- `StringSplit` - Split into array
- `StringToUpperCase` - Convert to uppercase
- `StringToLowerCase` - Convert to lowercase
- `StringTrim` - Remove whitespace
- `StringReplace` - Replace substring

### Object Operations (extend existing)
- `ObjectKeys` - Get property keys
- `ObjectValues` - Get property values
- `ObjectEntries` - Get key-value pairs
- `ObjectAssign` - Merge objects
- `ObjectFreeze` - Make immutable

## Implementation Plan

### Step 1: Pass Infrastructure (this PR)
- [ ] Create `HIRPass.h` with base class
- [ ] Create `PassManager.h/.cpp`
- [ ] Add pass infrastructure to CMakeLists.txt
- [ ] Unit tests for pass manager

### Step 2: MethodResolutionPass
- [ ] Implement pass that recognizes array methods
- [ ] Transform CallMethod → ArrayPush (and others)
- [ ] Unit tests

### Step 3: BuiltinResolutionPass
- [ ] Handle console.log, Math.*, etc.
- [ ] Unit tests

### Step 4: Integration
- [ ] Update Driver to use PassManager
- [ ] Add --dump-hir-passes flag
- [ ] End-to-end tests

## Testing Strategy

Each pass should have:

1. **Unit tests** - Test the pass in isolation with hand-crafted HIR
2. **Golden tests** - Compare pass output against expected output
3. **Integration tests** - Full pipeline tests with TypeScript source

Example unit test:

```cpp
TEST(MethodResolutionPass, TransformsArrayPush) {
    HIRModule module;
    // Build test HIR...

    MethodResolutionPass pass;
    auto result = pass.run(module);

    EXPECT_TRUE(result.changed);
    EXPECT_EQ(findInstruction(module, HIROpcode::ArrayPush), 1);
    EXPECT_EQ(findInstruction(module, HIROpcode::CallMethod), 0);
}
```

## Open Questions

1. **When to box/unbox?** Should method resolution also insert box/unbox operations,
   or is that a separate pass?

2. **Polymorphic calls?** How do we handle `let x = arr.push.bind(arr)`? Leave as
   CallMethod for runtime dispatch?

3. **Analysis invalidation?** When a pass modifies IR, how do we invalidate cached
   analysis results?

4. **Error handling?** Should passes be able to emit diagnostics/warnings?

## References

- [LLVM Pass Infrastructure](https://llvm.org/docs/WritingAnLLVMPass.html)
- [MLIR Pass Infrastructure](https://mlir.llvm.org/docs/PassManagement/)
- [GraalVM Truffle](https://www.graalvm.org/latest/graalvm-as-a-platform/language-implementation-framework/)
