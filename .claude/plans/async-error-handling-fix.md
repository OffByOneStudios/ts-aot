# Async Error Handling Fix Plan

**Status:** Investigation Required
**Created:** 2026-01-06
**Context:** Try-catch blocks in async functions not catching Promise rejections

## Problem Summary

Async functions with try-catch blocks fail to catch errors from:
1. `await Promise.reject('error')`
2. `await throwErrorAsync()` (async function that throws)
3. Error propagation through async call chains

**Current Test Results:** async_error.ts has 7/8 tests failing
- Only the "Finally block execution" test passes
- All try-catch error catching tests fail

## Root Cause Analysis

### Code Generation Status: ✅ CORRECT

The compiler correctly generates:
1. **pendingExc frame variable** for all async functions with try-catch
2. **Error handling in emitAwait()** that stores exceptions in pendingExc (lines 80-91)
3. **Catch block loading** from pendingExc

Example IR (correct):
```llvm
; When await fails (error flag set)
%resumedValue = load ptr, ptr %resumedValuePtr
store ptr %resumedValue, ptr %pendingExc  ; Store in catch variable
call void @ts_set_exception(ptr %resumedValue)  ; Also set global
br label %catch_block
```

### Runtime Issue: ❌ BROKEN

The problem is in Promise rejection propagation:

**Expected Flow:**
```
Promise.reject('error')
  → ts_promise_reject_internal() sets promise->state = Rejected
  → Microtask runs ts_promise_run_callback()
  → Line 246: asyncCtx->error = true  ← THIS SHOULD HAPPEN
  → ts_async_resume(asyncCtx, rejectedValue)
  → State machine checks error flag, branches to error handler
```

**Actual Behavior:**
- `asyncCtx->error` is NOT being set
- State machine continues to next state without error handling
- Exception is lost

## Investigation Plan

### Phase 1: Verify Promise Rejection Flow (1-2 hours)

**Test Files to Create:**

1. **test_promise_reject_minimal.ts** - Simplest possible case
```typescript
async function user_main(): Promise<number> {
  console.log('Before reject');
  await Promise.reject('test error');
  console.log('FAIL: After reject - should not reach');
  return 0;
}
```

2. **test_promise_reject_catch.ts** - With try-catch
```typescript
async function user_main(): Promise<number> {
  try {
    await Promise.reject('test error');
    console.log('FAIL: After reject');
  } catch (e) {
    console.log('PASS: Caught error');
  }
  return 0;
}
```

**Debug Instrumentation:**

Add to `src/runtime/src/TsPromise.cpp`:

```cpp
// In ts_promise_reject_internal() (line 355)
void ts_promise_reject_internal(TsPromise* promise, void* value) {
    fprintf(stderr, "DEBUG: ts_promise_reject_internal promise=%p\n", promise);
    if (promise->state != PromiseState::Pending) {
        fprintf(stderr, "DEBUG: Promise already settled, state=%d\n", (int)promise->state);
        return;
    }
    promise->state = PromiseState::Rejected;
    promise->value = value;
    fprintf(stderr, "DEBUG: Promise state set to Rejected, callbacks=%zu\n",
            promise->onFulfilled.size() + promise->onRejected.size());
    // ... rest of function
}

// In ts_promise_run_callback() (line 244)
void ts_promise_run_callback(void* data) {
    auto* cb = (PromiseCallback*)data;
    fprintf(stderr, "DEBUG: ts_promise_run_callback cb=%p, isAsync=%d, promise->state=%d\n",
            cb, cb->isAsyncContext, (int)cb->promise->state);

    if (cb->isAsyncContext) {
        AsyncContext* asyncCtx = (AsyncContext*)cb->callback;
        fprintf(stderr, "DEBUG: AsyncContext=%p, promise->state=%d\n",
                asyncCtx, (int)cb->promise->state);

        if (cb->promise->state == PromiseState::Rejected) {
            asyncCtx->error = true;  // Line 246 - CHECK IF THIS EXECUTES
            fprintf(stderr, "DEBUG: Set asyncCtx->error = true\n");
        }
        // ... rest
    }
    // ... rest
}
```

**Expected Output:**
```
DEBUG: ts_promise_reject_internal promise=0x...
DEBUG: Promise state set to Rejected, callbacks=1
DEBUG: ts_promise_run_callback cb=0x..., isAsync=1, promise->state=2
DEBUG: AsyncContext=0x..., promise->state=2
DEBUG: Set asyncCtx->error = true
```

**If Line 246 NOT Executing:**
- Problem: Callback not registered as async context
- Check: `then_async()` registration (line 721)

**If Line 246 IS Executing but catch still fails:**
- Problem: State machine not checking error flag correctly
- Check: emitAwait() error handling IR generation

### Phase 2: Check Callback Registration (30 min)

Verify that await correctly registers AsyncContext as callback:

```cpp
// In then_async() (line 721)
extern "C" void* then_async(void* promisePtr, void* asyncContextPtr) {
    fprintf(stderr, "DEBUG: then_async promise=%p, asyncCtx=%p\n",
            promisePtr, asyncContextPtr);
    TsPromise* promise = (TsPromise*)promisePtr;
    AsyncContext* asyncCtx = (AsyncContext*)asyncContextPtr;

    auto* cb = new PromiseCallback();
    cb->callback = asyncCtx;
    cb->promise = promise;
    cb->isAsyncContext = true;  // VERIFY THIS IS SET
    fprintf(stderr, "DEBUG: Callback registered, isAsyncContext=%d\n", cb->isAsyncContext);

    // ... rest
}
```

### Phase 3: Check State Machine Error Handling (30 min)

Compile test with `--dump-ir` and verify error path:

```bash
./build/src/compiler/Release/ts-aot.exe tmp/test_promise_reject_catch.ts --dump-ir > tmp/reject.ll
```

Look for in state machine:
```llvm
; Load error flag (field 4 of AsyncContext)
%errorFlagPtr = getelementptr inbounds %AsyncContext, ptr %ctx, i32 0, i32 4
%errorFlag = load i8, ptr %errorFlagPtr

; Branch on error
%isError = icmp ne i8 %errorFlag, 0
br i1 %isError, label %error_handler, label %success_handler

error_handler:
  ; Should load pendingExc and branch to catch block
  %pendingExcPtr = getelementptr inbounds %Frame, ptr %frame, i32 0, i32 X
  %exception = load ptr, ptr %pendingExcPtr
  store ptr %exception, ptr %catchVar
  br label %catch_block
```

**If error path missing:**
- Problem: emitAwait() not generating error handling
- Fix: Check lines 80-91 of IRGenerator_Expressions_Async.cpp

### Phase 4: Check AsyncContext Structure Layout (15 min)

Verify field indices match between compiler and runtime:

**Compiler** (IRGenerator_Core.cpp):
```cpp
asyncContextType = llvm::StructType::create(*context,
    {
        builder->getInt32Ty(),  // 0: state
        builder->getInt8Ty(),   // 1: error      ← Field 1
        // ... rest
    },
    "AsyncContext"
);
```

**Runtime** (TsPromise.h):
```cpp
struct AsyncContext : public TsObject {
    int state = 0;          // Field 0 (after vtable/TsObject)
    bool error = false;     // Field 1
    // ... rest
};
```

**Verification:** Compile with debug, check IR field access matches struct layout.

## Potential Root Causes (Ranked by Likelihood)

### 1. Microtask Queue Not Running (HIGH)

**Symptom:** Callbacks never execute
**Test:** Add debug output to `ts_run_microtasks()`
**Fix:** Ensure microtask queue is pumped after Promise.reject()

### 2. Callback Not Registered as AsyncContext (MEDIUM)

**Symptom:** Line 246 not reached because `cb->isAsyncContext` is false
**Test:** Debug output in `then_async()`
**Fix:** Ensure `isAsyncContext` flag set correctly

### 3. AsyncContext Field Index Mismatch (MEDIUM)

**Symptom:** Writing to wrong field, error flag in wrong location
**Test:** Verify struct layout matches between compiler and runtime
**Fix:** Correct field indices in asyncContextType definition

### 4. Promise.reject() Not Creating Rejected Promise (LOW)

**Symptom:** Promise state is Pending, not Rejected
**Test:** Debug output in `ts_promise_reject_internal()`
**Fix:** Check `ts_promise_reject()` implementation

### 5. State Machine Error Check Missing (LOW)

**Symptom:** Error flag set but not checked
**Test:** Examine IR for error flag load and branch
**Fix:** Ensure emitAwait() generates error handling

## Proposed Fixes

### Fix Option A: Immediate Rejection Handling (FAST, LOW RISK)

For synchronously rejected promises (Promise.reject()), handle immediately:

```cpp
// In emitAwait() after calling then_async()
// Check if promise is already rejected
llvm::Value* promiseStatePtr = builder->CreateStructGEP(promiseType, awaitedValue, 0);
llvm::Value* promiseState = builder->CreateLoad(builder->getInt32Ty(), promiseStatePtr);

llvm::Value* isRejected = builder->CreateICmpEQ(
    promiseState,
    llvm::ConstantInt::get(builder->getInt32Ty(), 2)  // Rejected = 2
);

llvm::BasicBlock* syncRejectedBB = llvm::BasicBlock::Create(*context, "sync_rejected", currentFunction);
llvm::BasicBlock* asyncWaitBB = llvm::BasicBlock::Create(*context, "async_wait", currentFunction);

builder->CreateCondBr(isRejected, syncRejectedBB, asyncWaitBB);

builder->SetInsertPoint(syncRejectedBB);
// Handle synchronous rejection immediately
if (!catchStack.empty()) {
    llvm::Value* rejectedValue = builder->CreateLoad(builder->getPtrTy(), promiseValuePtr);
    builder->CreateStore(rejectedValue, catchStack.back().pendingExc);
    builder->CreateBr(catchStack.back().catchBB);
} else {
    // Unhandled rejection - abort or log
}

builder->SetInsertPoint(asyncWaitBB);
// Continue with normal async path
```

**Pros:**
- Simple, localized change
- Handles most test cases (Promise.reject is synchronous)
- Low regression risk

**Cons:**
- Doesn't fix async error propagation
- Still need runtime fix for thrown errors

### Fix Option B: Fix Promise Runtime (THOROUGH, HIGHER RISK)

Debug and fix the microtask/callback execution:

1. Add comprehensive logging to trace execution
2. Verify `ts_run_microtasks()` is called after Promise.reject()
3. Ensure callback queue is properly populated
4. Fix any timing issues in callback execution

**Pros:**
- Fixes all error cases (sync and async)
- Proper solution

**Cons:**
- More complex debugging
- Higher risk of breaking other Promise functionality

### Fix Option C: Alternative Error Propagation (NUCLEAR OPTION)

If runtime callback system is too complex, use alternative mechanism:

```cpp
// Store rejected promise in AsyncContext
struct AsyncContext {
    // ... existing fields
    TsPromise* pendingRejection = nullptr;  // NEW FIELD
};

// In emitAwait(), after then_async():
// Check for immediate rejection and store
if (isRejected) {
    llvm::Value* rejectionPtr = builder->CreateStructGEP(asyncContextType, ctx, REJECTION_FIELD);
    builder->CreateStore(awaitedValue, rejectionPtr);
}

// In state machine resume, check for stored rejection:
llvm::Value* rejectionPtr = builder->CreateStructGEP(asyncContextType, ctx, REJECTION_FIELD);
llvm::Value* rejection = builder->CreateLoad(builder->getPtrTy(), rejectionPtr);
llvm::Value* hasRejection = builder->CreateICmpNE(rejection, llvm::ConstantPointerNull::get(builder->getPtrTy()));
br i1 %hasRejection, label %handle_rejection, label %normal_resume
```

**Pros:**
- Bypasses broken callback system
- Guaranteed to work

**Cons:**
- Workaround, not a fix
- Duplicates error handling logic

## Recommended Approach

**Phase 1:** Investigation (1-2 hours)
- Add debug instrumentation
- Run minimal test cases
- Identify exact failure point

**Phase 2:** Implement Fix A (30 min)
- Add synchronous rejection handling in emitAwait()
- Test with async_error.ts
- Should fix 5-6 of the 7 failing tests

**Phase 3:** If Fix A insufficient, implement Fix B (2-3 hours)
- Debug Promise runtime callback execution
- Fix microtask queue or callback registration
- Test all async error cases

**Phase 4:** Verification
- Run full async_error.ts suite (expect 8/8 passing)
- Run async_basic.ts (ensure no regression)
- Run golden IR tests (ensure no regression)

## Success Criteria

- ✅ `async_error.ts` passes 8/8 tests
- ✅ No regression in `async_basic.ts`
- ✅ No regression in golden IR test suite
- ✅ All three error types work:
  - Direct `await Promise.reject()`
  - `await throwErrorAsync()`
  - Error propagation through async calls

## Estimated Timeline

- **Investigation:** 1-2 hours
- **Fix A Implementation:** 30 minutes
- **Fix B Implementation (if needed):** 2-3 hours
- **Testing & Verification:** 1 hour
- **Total:** 3-6 hours depending on complexity

## Files to Modify

### Compiler
- `src/compiler/codegen/IRGenerator_Expressions_Async.cpp` (emitAwait error handling)
- Possibly `src/compiler/codegen/IRGenerator_Core.cpp` (AsyncContext structure)

### Runtime (Debug Only)
- `src/runtime/src/TsPromise.cpp` (add logging, possibly fix callback execution)
- `src/runtime/include/TsPromise.h` (possibly add rejection field)

### Tests
- Create `tmp/test_promise_reject_minimal.ts`
- Create `tmp/test_promise_reject_catch.ts`
- Use existing `tests/node/async/async_error.ts` for verification
