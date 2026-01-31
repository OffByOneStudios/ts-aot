---
paths: src/compiler/codegen/**
priority: high
---

# LLVM 18 IR Generation Patterns

This project uses **LLVM 18** with **opaque pointers**. These rules ensure correct IR generation.

## Opaque Pointers (LLVM 15+)

LLVM 18 uses opaque pointers - all pointers are just `ptr`, not `i32*` or `%struct.Foo*`.

### Pointer Types

**❌ WRONG (LLVM 14 and earlier):**
```cpp
llvm::Type* ptrType = intType->getPointerTo();  // Removed in LLVM 15+
llvm::PointerType::get(structType, 0);           // Old API
```

**✅ CORRECT (LLVM 18):**
```cpp
llvm::Type* ptrType = builder->getPtrTy();      // All pointers
llvm::Type* ptrType = llvm::PointerType::get(context, 0);  // Alternative
```

### GEP (GetElementPtr)

**❌ WRONG:**
```cpp
// Missing source type - will fail!
builder->CreateGEP(ptr, indices);
```

**✅ CORRECT:**
```cpp
// ALWAYS provide source element type
builder->CreateGEP(structType, ptr, indices);
builder->CreateStructGEP(structType, ptr, fieldIndex);
builder->CreateInBoundsGEP(arrayType, ptr, indices);
```

**Example:**
```cpp
// Get field 2 from struct
llvm::Type* structType = llvm::StructType::get(
    context,
    { builder->getInt64Ty(), builder->getPtrTy(), builder->getDoubleTy() }
);
llvm::Value* fieldPtr = builder->CreateStructGEP(structType, structPtr, 2);
```

### Load/Store

**❌ WRONG:**
```cpp
// Missing value type - will fail!
builder->CreateLoad(ptr);
```

**✅ CORRECT:**
```cpp
// ALWAYS provide value type
builder->CreateLoad(builder->getInt64Ty(), ptr);
builder->CreateLoad(builder->getPtrTy(), ptr);
builder->CreateLoad(builder->getDoubleTy(), ptr);

// Store doesn't need type (inferred from value)
builder->CreateStore(value, ptr);
```

### Function Calls

**❌ WRONG:**
```cpp
// Type mismatch - args don't match FunctionType!
llvm::FunctionType* ft = llvm::FunctionType::get(
    builder->getPtrTy(),
    { builder->getInt64Ty() },  // Expected: i64
    false
);
builder->CreateCall(fn, { ptrValue });  // Passed: ptr - CRASH!
```

**✅ CORRECT:**
```cpp
// ALWAYS provide FunctionType and match argument types exactly
llvm::FunctionType* ft = llvm::FunctionType::get(
    builder->getPtrTy(),
    { builder->getInt64Ty() },
    false
);

// Cast argument to match type if needed
llvm::Value* arg = builder->CreatePtrToInt(ptrValue, builder->getInt64Ty());
llvm::Value* result = builder->CreateCall(ft, fn.getCallee(), { arg });
```

**Using createCall helper:**
```cpp
llvm::FunctionType* ft = llvm::FunctionType::get(
    builder->getPtrTy(),
    { builder->getPtrTy(), builder->getInt64Ty() },
    false
);
llvm::FunctionCallee fn = getRuntimeFunction("ts_array_get", ft);
llvm::Value* result = createCall(ft, fn.getCallee(), { arrayPtr, indexValue });
```

## Type Patterns

### Common Type Mappings

| TypeScript/Runtime | Analyzer | IRGenerator | LLVM Type |
|-------------------|----------|-------------|-----------|
| `number` | `TypeKind::Number` | N/A | `builder->getDoubleTy()` |
| `int` (runtime) | `TypeKind::Int` | N/A | `builder->getInt64Ty()` |
| `boolean` | `TypeKind::Boolean` | N/A | `builder->getInt1Ty()` |
| `string` | `TypeKind::String` | N/A | `builder->getPtrTy()` (TsString*) |
| `any` | `TypeKind::Any` | N/A | `builder->getPtrTy()` (TsValue*) |
| Object/Class | `ClassType` | N/A | `builder->getPtrTy()` |
| Array | `ArrayType` | N/A | `builder->getPtrTy()` (TsArray*) |
| Function | `FunctionType` | N/A | `builder->getPtrTy()` |

### Creating Types

```cpp
// Primitives
llvm::Type* i1 = builder->getInt1Ty();      // bool
llvm::Type* i8 = builder->getInt8Ty();      // byte
llvm::Type* i32 = builder->getInt32Ty();    // int32
llvm::Type* i64 = builder->getInt64Ty();    // int64
llvm::Type* f64 = builder->getDoubleTy();   // double
llvm::Type* ptr = builder->getPtrTy();      // any pointer
llvm::Type* voidTy = builder->getVoidTy();  // void

// Struct types
llvm::StructType* structTy = llvm::StructType::get(
    context,
    { builder->getInt64Ty(), builder->getPtrTy() }
);

// Function types
llvm::FunctionType* funcTy = llvm::FunctionType::get(
    builder->getPtrTy(),                    // Return type
    { builder->getPtrTy(), builder->getInt64Ty() },  // Param types
    false                                   // Not vararg
);
```

## Boxing & Unboxing Patterns

### Boxing

```cpp
// Box an integer
llvm::Value* boxed = boxValue(intValue, intType);
boxedValues.insert(boxed);  // ALWAYS track boxed values!

// Manual boxing
llvm::FunctionType* ft = llvm::FunctionType::get(
    builder->getPtrTy(),
    { builder->getInt64Ty() },
    false
);
llvm::FunctionCallee fn = getRuntimeFunction("ts_value_make_int", ft);
llvm::Value* boxed = createCall(ft, fn.getCallee(), { intValue });
boxedValues.insert(boxed);
```

### Unboxing

```cpp
// Check if value is boxed
if (boxedValues.count(value)) {
    // Value is boxed, emit unbox call
    llvm::FunctionType* ft = llvm::FunctionType::get(
        builder->getInt64Ty(),
        { builder->getPtrTy() },
        false
    );
    llvm::FunctionCallee fn = getRuntimeFunction("ts_value_get_int", ft);
    value = createCall(ft, fn.getCallee(), { value });
}

// For TypeKind::Any, ALWAYS unbox unconditionally
if (type->kind == TypeKind::Any) {
    // Don't check boxedValues - loaded values aren't tracked!
    llvm::FunctionType* ft = llvm::FunctionType::get(
        builder->getPtrTy(),
        { builder->getPtrTy() },
        false
    );
    llvm::FunctionCallee fn = getRuntimeFunction("ts_value_get_object", ft);
    value = createCall(ft, fn.getCallee(), { value });
}
```

### Unboxing Functions

| Runtime Function | Input Type | Output Type | Use Case |
|-----------------|------------|-------------|----------|
| `ts_value_get_object` | `ptr` (TsValue*) | `ptr` (raw) | Extract object pointer |
| `ts_value_get_int` | `ptr` (TsValue*) | `i64` | Extract integer |
| `ts_value_get_double` | `ptr` (TsValue*) | `double` | Extract double |
| `ts_value_get_bool` | `ptr` (TsValue*) | `i1` | Extract boolean |
| `ts_value_get_string` | `ptr` (TsValue*) | `ptr` (TsString*) | Extract string |

## Control Flow

### If-Else

```cpp
// Create basic blocks
llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(context, "if.then", currentFunction);
llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(context, "if.else", currentFunction);
llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context, "if.end", currentFunction);

// Branch on condition
llvm::Value* condition = visitExpression(ifStmt->condition.get());
builder->CreateCondBr(condition, thenBB, elseBB);

// Then block
builder->SetInsertPoint(thenBB);
visitStatement(ifStmt->thenStatement.get());
if (!builder->GetInsertBlock()->getTerminator()) {
    builder->CreateBr(mergeBB);
}

// Else block
builder->SetInsertPoint(elseBB);
if (ifStmt->elseStatement) {
    visitStatement(ifStmt->elseStatement.get());
}
if (!builder->GetInsertBlock()->getTerminator()) {
    builder->CreateBr(mergeBB);
}

// Merge block
builder->SetInsertPoint(mergeBB);
```

### Loops

```cpp
// While loop structure
llvm::BasicBlock* condBB = llvm::BasicBlock::Create(context, "while.cond", currentFunction);
llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "while.body", currentFunction);
llvm::BasicBlock* exitBB = llvm::BasicBlock::Create(context, "while.end", currentFunction);

// Jump to condition
builder->CreateBr(condBB);

// Condition block
builder->SetInsertPoint(condBB);
llvm::Value* cond = visitExpression(whileLoop->condition.get());
builder->CreateCondBr(cond, bodyBB, exitBB);

// Body block
builder->SetInsertPoint(bodyBB);
visitStatement(whileLoop->body.get());
if (!builder->GetInsertBlock()->getTerminator()) {
    builder->CreateBr(condBB);  // Loop back
}

// Exit block
builder->SetInsertPoint(exitBB);
```

## Function Calls

### Runtime Function Calls

```cpp
// Pattern for calling runtime functions
llvm::FunctionType* ft = llvm::FunctionType::get(
    builder->getPtrTy(),                           // Return type
    { builder->getPtrTy(), builder->getInt64Ty() }, // Argument types
    false                                          // Not variadic
);

llvm::FunctionCallee fn = getRuntimeFunction("ts_array_get", ft);
llvm::Value* result = createCall(ft, fn.getCallee(), { arrayPtr, indexValue });
```

### User Function Calls

```cpp
// Get function pointer from symbol table
llvm::Function* userFunc = module->getFunction(funcName);
if (!userFunc) {
    throw std::runtime_error("Unknown function: " + funcName);
}

// Prepare arguments
std::vector<llvm::Value*> args;
for (auto& arg : callExpr->arguments) {
    args.push_back(visitExpression(arg.get()));
}

// Call function
llvm::Value* result = createCall(
    userFunc->getFunctionType(),
    userFunc,
    args
);
```

## Type Casting

### Integer Conversions

```cpp
// i1 (bool) to i64
llvm::Value* extended = builder->CreateZExt(boolValue, builder->getInt64Ty());

// i64 to i1 (bool)
llvm::Value* truncated = builder->CreateICmpNE(
    intValue,
    llvm::ConstantInt::get(builder->getInt64Ty(), 0)
);

// i32 to i64
llvm::Value* extended = builder->CreateSExt(i32Value, builder->getInt64Ty());
```

### Pointer Conversions

```cpp
// ptr to i64
llvm::Value* intVal = builder->CreatePtrToInt(ptrValue, builder->getInt64Ty());

// i64 to ptr
llvm::Value* ptrVal = builder->CreateIntToPtr(intValue, builder->getPtrTy());

// No cast needed between different pointer types (opaque pointers)
llvm::Value* typedPtr = genericPtr;  // Just use directly
```

### Float Conversions

```cpp
// i64 to double
llvm::Value* floatVal = builder->CreateSIToFP(intValue, builder->getDoubleTy());

// double to i64
llvm::Value* intVal = builder->CreateFPToSI(doubleValue, builder->getInt64Ty());
```

### TypeScript Numbers and Bitwise Operations

**⚠️ IMPORTANT:** TypeScript/JavaScript numbers are IEEE 754 doubles (`f64`), but LLVM bitwise operations require integers (`i64`). Always convert!

```cpp
// Helper pattern for bitwise ops
llvm::Value* ensureI64ForBitwise(llvm::Value* val) {
    if (val->getType()->isDoubleTy()) {
        return builder_->CreateFPToSI(val, builder_->getInt64Ty(), "toi64");
    }
    return val;
}

// Usage in bitwise AND
llvm::Value* lhs = ensureI64ForBitwise(getOperandValue(inst->operands[0]));
llvm::Value* rhs = ensureI64ForBitwise(getOperandValue(inst->operands[1]));
llvm::Value* result = builder_->CreateAnd(lhs, rhs, "and");
// Convert result back to f64 for JS semantics
result = builder_->CreateSIToFP(result, builder_->getDoubleTy(), "tof64");
```

This applies to: `and`, `or`, `xor`, `shl`, `shr`, `ushr`, `not`

## Constant Values

```cpp
// Integer constants
llvm::Constant* zero = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
llvm::Constant* one = llvm::ConstantInt::get(builder->getInt64Ty(), 1);
llvm::Constant* negOne = llvm::ConstantInt::get(builder->getInt64Ty(), -1, true);

// Boolean constants
llvm::Constant* trueVal = llvm::ConstantInt::getTrue(context);
llvm::Constant* falseVal = llvm::ConstantInt::getFalse(context);

// Float constants
llvm::Constant* pi = llvm::ConstantFP::get(builder->getDoubleTy(), 3.14159);

// Null pointer
llvm::Constant* nullPtr = llvm::ConstantPointerNull::get(builder->getPtrTy());
```

## Common Pitfalls

### 1. Type Mismatches in CreateCall

```cpp
// BAD - argument type doesn't match FunctionType
llvm::FunctionType* ft = llvm::FunctionType::get(
    builder->getPtrTy(),
    { builder->getInt64Ty() },
    false
);
builder->CreateCall(ft, fn, { ptrValue });  // ptr != i64 - CRASH!

// GOOD - cast argument to match
llvm::Value* intArg = builder->CreatePtrToInt(ptrValue, builder->getInt64Ty());
builder->CreateCall(ft, fn, { intArg });
```

### 2. Forgetting to Track Boxed Values

```cpp
// BAD
llvm::Value* boxed = boxValue(value, type);
// Missing: boxedValues.insert(boxed);

// GOOD
llvm::Value* boxed = boxValue(value, type);
boxedValues.insert(boxed);  // Track it!
```

### 3. Checking boxedValues for Loaded Values

```cpp
// BAD - loaded values aren't in boxedValues
llvm::Value* loaded = builder->CreateLoad(builder->getPtrTy(), allocaPtr);
if (boxedValues.count(loaded)) {  // Always false!
    // Won't execute
}

// GOOD - For Any type, always unbox
if (type->kind == TypeKind::Any) {
    // Unconditionally unbox
    loaded = unboxValue(loaded, type);
}
```

### 4. Missing Source Type in GEP

```cpp
// BAD
builder->CreateGEP(ptr, indices);  // Missing source type!

// GOOD
builder->CreateGEP(elementType, ptr, indices);
```

## Quick Reference

| Operation | LLVM API |
|-----------|----------|
| Get pointer type | `builder->getPtrTy()` |
| GEP | `builder->CreateGEP(srcType, ptr, indices)` |
| Struct GEP | `builder->CreateStructGEP(structType, ptr, index)` |
| Load | `builder->CreateLoad(valueType, ptr)` |
| Store | `builder->CreateStore(value, ptr)` |
| Call | `builder->CreateCall(funcType, fn, args)` |
| Cast ptr→i64 | `builder->CreatePtrToInt(ptr, i64Ty)` |
| Cast i64→ptr | `builder->CreateIntToPtr(i64, ptrTy)` |
| Cast i64→double | `builder->CreateSIToFP(i64, doubleTy)` |
| Cast double→i64 | `builder->CreateFPToSI(double, i64Ty)` |
| Extend bool→i64 | `builder->CreateZExt(bool, i64Ty)` |
| Compare i64≠0 | `builder->CreateICmpNE(i64, zero)` |
