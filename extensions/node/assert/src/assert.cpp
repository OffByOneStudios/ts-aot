// TsAssert.cpp - Node.js assert module implementation

#include "TsAssert.h"
#include "TsString.h"
#include "TsObject.h"
#include "TsRuntime.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsRegExp.h"
#include "TsPromise.h"
#include "GC.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csetjmp>

// Exception context for assert.throws/doesNotThrow
struct AssertExceptionContext {
    jmp_buf env;
    bool caught;
    TsValue* exception;
};

// Context for async assert operations (rejects/doesNotReject)
struct AsyncAssertContext {
    ts::TsPromise* resultPromise;
    void* expectedError;    // Optional: error class/regex/message to match
    void* assertMessage;    // Optional: custom assertion message
};

// Helper to get string value from TsValue
static const char* getMessageStr(void* message) {
    if (!message) return nullptr;
    TsValue val = nanbox_to_tagged((TsValue*)message);
    if (val.type == ValueType::STRING_PTR) {
        TsString* str = (TsString*)val.ptr_val;
        return str ? str->ToUtf8() : nullptr;
    }
    // Try direct string pointer (fallback for raw TsString*)
    uint64_t nb = (uint64_t)(uintptr_t)message;
    if (nanbox_is_ptr(nb)) {
        void* ptr = nanbox_to_ptr(nb);
        if (ptr && *(uint32_t*)ptr == TsString::MAGIC) {
            return ((TsString*)ptr)->ToUtf8();
        }
    }
    return nullptr;
}

// Helper to check if value is truthy
static bool isTruthy(void* value) {
    if (!value) return false;
    TsValue val = nanbox_to_tagged((TsValue*)value);

    switch (val.type) {
        case ValueType::NUMBER_INT:
            return val.i_val != 0;
        case ValueType::NUMBER_DBL:
            return val.d_val != 0.0;
        case ValueType::BOOLEAN:
            return val.b_val;
        case ValueType::UNDEFINED:
            return false;
        case ValueType::STRING_PTR: {
            TsString* str = (TsString*)val.ptr_val;
            return str && strlen(str->ToUtf8()) > 0;
        }
        case ValueType::OBJECT_PTR:
            return val.ptr_val != nullptr;
        default:
            return true;
    }
}

// Helper to check loose equality (==)
static bool looseEqual(void* a, void* b) {
    if (a == b) return true;
    if (!a || !b) return false;

    TsValue va = nanbox_to_tagged((TsValue*)a);
    TsValue vb = nanbox_to_tagged((TsValue*)b);

    // Both undefined
    if (va.type == ValueType::UNDEFINED && vb.type == ValueType::UNDEFINED) {
        return true;
    }

    // Numbers
    if (va.type == ValueType::NUMBER_INT && vb.type == ValueType::NUMBER_INT) {
        return va.i_val == vb.i_val;
    }
    if (va.type == ValueType::NUMBER_DBL && vb.type == ValueType::NUMBER_DBL) {
        return va.d_val == vb.d_val;
    }
    if ((va.type == ValueType::NUMBER_INT || va.type == ValueType::NUMBER_DBL) &&
        (vb.type == ValueType::NUMBER_INT || vb.type == ValueType::NUMBER_DBL)) {
        double da = va.type == ValueType::NUMBER_INT ? (double)va.i_val : va.d_val;
        double db = vb.type == ValueType::NUMBER_INT ? (double)vb.i_val : vb.d_val;
        return da == db;
    }

    // Booleans
    if (va.type == ValueType::BOOLEAN && vb.type == ValueType::BOOLEAN) {
        return va.b_val == vb.b_val;
    }

    // Strings
    if (va.type == ValueType::STRING_PTR && vb.type == ValueType::STRING_PTR) {
        TsString* sa = (TsString*)va.ptr_val;
        TsString* sb = (TsString*)vb.ptr_val;
        if (!sa || !sb) return sa == sb;
        return strcmp(sa->ToUtf8(), sb->ToUtf8()) == 0;
    }

    // Object identity
    if (va.type == ValueType::OBJECT_PTR && vb.type == ValueType::OBJECT_PTR) {
        return va.ptr_val == vb.ptr_val;
    }

    return false;
}

// Helper to check strict equality (===)
static bool strictEqual(void* a, void* b) {
    if (a == b) return true;
    if (!a || !b) return false;

    TsValue va = nanbox_to_tagged((TsValue*)a);
    TsValue vb = nanbox_to_tagged((TsValue*)b);

    // In JavaScript, all numbers are the same type (IEEE 754 double).
    // Handle NUMBER_INT vs NUMBER_DBL cross-comparison as same type.
    bool aIsNum = (va.type == ValueType::NUMBER_INT || va.type == ValueType::NUMBER_DBL);
    bool bIsNum = (vb.type == ValueType::NUMBER_INT || vb.type == ValueType::NUMBER_DBL);
    if (aIsNum && bIsNum) {
        double da = va.type == ValueType::NUMBER_INT ? (double)va.i_val : va.d_val;
        double db = vb.type == ValueType::NUMBER_INT ? (double)vb.i_val : vb.d_val;
        return da == db;
    }

    // Handle STRING_PTR vs OBJECT_PTR containing strings
    bool aIsStr = (va.type == ValueType::STRING_PTR || va.type == ValueType::OBJECT_PTR);
    bool bIsStr = (vb.type == ValueType::STRING_PTR || vb.type == ValueType::OBJECT_PTR);

    // Must be same type for strict equality (except number cross-types handled above)
    if (va.type != vb.type && !(aIsStr && bIsStr)) return false;

    switch (va.type) {
        case ValueType::NUMBER_INT:
            return va.i_val == vb.i_val;
        case ValueType::NUMBER_DBL:
            return va.d_val == vb.d_val;
        case ValueType::BOOLEAN:
            return va.b_val == vb.b_val;
        case ValueType::UNDEFINED:
            return true;
        case ValueType::STRING_PTR: {
            TsString* sa = (TsString*)va.ptr_val;
            TsString* sb = (TsString*)vb.ptr_val;
            if (!sa || !sb) return sa == sb;
            return strcmp(sa->ToUtf8(), sb->ToUtf8()) == 0;
        }
        case ValueType::OBJECT_PTR:
            // If the other is a string type, compare as strings
            if (vb.type == ValueType::STRING_PTR) {
                TsString* sa = (TsString*)va.ptr_val;
                TsString* sb = (TsString*)vb.ptr_val;
                if (!sa || !sb) return sa == sb;
                return strcmp(sa->ToUtf8(), sb->ToUtf8()) == 0;
            }
            return va.ptr_val == vb.ptr_val;
        default:
            return false;
    }
}

// Forward declarations for deep comparison
static bool deepStrictEqual(void* a, void* b);
static bool deepEqualObjects(TsMap* a, TsMap* b);

// Helper for deep array comparison
// Compare two raw array element values (int64_t from TsArray::Get)
// Elements may be: TsValue* (boxed), raw TsString*, raw TsArray*, or raw int64
static bool deepEqualElement(int64_t va, int64_t vb);

static bool deepEqualArrays(TsArray* a, TsArray* b) {
    if (a->Length() != b->Length()) return false;
    for (size_t i = 0; i < a->Length(); i++) {
        int64_t va = a->Get(i);
        int64_t vb = b->Get(i);
        if (va == vb) continue;
        if (!deepEqualElement(va, vb)) return false;
    }
    return true;
}

static bool deepEqualElement(int64_t va, int64_t vb) {
    if (va == vb) return true;
    // Array elements are NaN-boxed values - delegate to deepStrictEqual
    return deepStrictEqual((void*)(uintptr_t)(uint64_t)va, (void*)(uintptr_t)(uint64_t)vb);
}

// Helper for deep object comparison
static bool deepEqualObjects(TsMap* a, TsMap* b) {
    // Get keys as arrays
    TsArray* keysA = (TsArray*)a->GetKeys();
    TsArray* keysB = (TsArray*)b->GetKeys();
    if (!keysA || !keysB) return keysA == keysB;
    if (keysA->Length() != keysB->Length()) return false;

    for (size_t i = 0; i < keysA->Length(); i++) {
        // Keys are NaN-boxed values in the array
        TsValue key = nanbox_to_tagged((TsValue*)(uintptr_t)keysA->Get(i));
        if (key.type == ValueType::UNDEFINED) continue;
        TsValue valA = a->Get(key);
        TsValue valB = b->Get(key);
        if (!deepStrictEqual(nanbox_from_tagged(valA), nanbox_from_tagged(valB))) return false;
    }
    return true;
}

// Deep strict equality comparison
static bool deepStrictEqual(void* a, void* b) {
    if (a == b) return true;
    if (!a || !b) return a == b;

    TsValue va = nanbox_to_tagged((TsValue*)a);
    TsValue vb = nanbox_to_tagged((TsValue*)b);

    // In JavaScript, NUMBER_INT and NUMBER_DBL are the same type
    bool aIsNum = (va.type == ValueType::NUMBER_INT || va.type == ValueType::NUMBER_DBL);
    bool bIsNum = (vb.type == ValueType::NUMBER_INT || vb.type == ValueType::NUMBER_DBL);
    if (aIsNum && bIsNum) {
        double da = va.type == ValueType::NUMBER_INT ? (double)va.i_val : va.d_val;
        double db = vb.type == ValueType::NUMBER_INT ? (double)vb.i_val : vb.d_val;
        return da == db;
    }

    // Handle STRING_PTR vs OBJECT_PTR (strings may be boxed as either)
    bool aIsStr = (va.type == ValueType::STRING_PTR || va.type == ValueType::OBJECT_PTR);
    bool bIsStr = (vb.type == ValueType::STRING_PTR || vb.type == ValueType::OBJECT_PTR);

    // Must be same type for strict equality (except numbers and strings handled above)
    if (va.type != vb.type && !(aIsStr && bIsStr)) return false;

    switch (va.type) {
        case ValueType::NUMBER_INT:
            return va.i_val == vb.i_val;
        case ValueType::NUMBER_DBL:
            return va.d_val == vb.d_val;
        case ValueType::BOOLEAN:
            return va.b_val == vb.b_val;
        case ValueType::UNDEFINED:
            return true;
        case ValueType::STRING_PTR: {
            TsString* sa = (TsString*)va.ptr_val;
            TsString* sb = (TsString*)vb.ptr_val;
            if (!sa || !sb) return sa == sb;
            return strcmp(sa->ToUtf8(), sb->ToUtf8()) == 0;
        }
        case ValueType::ARRAY_PTR: {
            // Direct array type - compare as arrays
            TsArray* arrA = (TsArray*)va.ptr_val;
            TsArray* arrB = (TsArray*)vb.ptr_val;
            if (!arrA || !arrB) return arrA == arrB;
            return deepEqualArrays(arrA, arrB);
        }
        case ValueType::OBJECT_PTR: {
            void* pa = va.ptr_val;
            void* pb = vb.ptr_val;
            if (!pa || !pb) return pa == pb;

            // Both TsArray and TsString have magic at offset 0 (not TsObject-derived layout).
            // TsObject-derived types (TsMap) have magic at offset 16.
            uint32_t magicA = *(uint32_t*)pa;
            uint32_t magicB = *(uint32_t*)pb;

            if (magicA == TsArray::MAGIC && magicB == TsArray::MAGIC) {
                return deepEqualArrays((TsArray*)pa, (TsArray*)pb);
            }
            if (magicA == TsString::MAGIC && magicB == TsString::MAGIC) {
                return strcmp(((TsString*)pa)->ToUtf8(), ((TsString*)pb)->ToUtf8()) == 0;
            }

            // For TsObject-derived types (TsMap, etc.), magic is at TsObject's offset
            TsObject* oa = (TsObject*)pa;
            TsObject* ob = (TsObject*)pb;

            // Check if both are maps/objects
            if (oa->magic == TsMap::MAGIC && ob->magic == TsMap::MAGIC) {
                return deepEqualObjects((TsMap*)oa, (TsMap*)ob);
            }

            // Different types or same reference
            return pa == pb;
        }
        default:
            return false;
    }
}

// Assertion failure helper
static void assertionFailed(const char* op, void* actual, void* expected, void* message) {
    const char* msg = getMessageStr(message);

    fprintf(stderr, "AssertionError [ERR_ASSERTION]: ");
    if (msg && strlen(msg) > 0) {
        fprintf(stderr, "%s\n", msg);
    } else {
        fprintf(stderr, "The expression evaluated to a falsy value.\n");
    }
    fprintf(stderr, "  operator: %s\n", op);

    exit(1);
}

extern "C" {

void ts_assert(void* value, void* message) {
    if (!isTruthy(value)) {
        assertionFailed("==", value, ts_value_make_bool(true), message);
    }
}

void ts_assert_ok(void* value, void* message) {
    ts_assert(value, message);
}

void ts_assert_equal(void* actual, void* expected, void* message) {
    if (!looseEqual(actual, expected)) {
        assertionFailed("==", actual, expected, message);
    }
}

void ts_assert_not_equal(void* actual, void* expected, void* message) {
    if (looseEqual(actual, expected)) {
        assertionFailed("!=", actual, expected, message);
    }
}

void ts_assert_strict_equal(void* actual, void* expected, void* message) {
    if (!strictEqual(actual, expected)) {
        assertionFailed("===", actual, expected, message);
    }
}

void ts_assert_not_strict_equal(void* actual, void* expected, void* message) {
    if (strictEqual(actual, expected)) {
        assertionFailed("!==", actual, expected, message);
    }
}

void ts_assert_deep_equal(void* actual, void* expected, void* message) {
    if (!deepStrictEqual(actual, expected)) {  // Using deepStrict for deep comparisons
        assertionFailed("deepEqual", actual, expected, message);
    }
}

void ts_assert_not_deep_equal(void* actual, void* expected, void* message) {
    if (deepStrictEqual(actual, expected)) {
        assertionFailed("notDeepEqual", actual, expected, message);
    }
}

void ts_assert_deep_strict_equal(void* actual, void* expected, void* message) {
    if (!deepStrictEqual(actual, expected)) {
        assertionFailed("deepStrictEqual", actual, expected, message);
    }
}

void ts_assert_not_deep_strict_equal(void* actual, void* expected, void* message) {
    if (deepStrictEqual(actual, expected)) {
        assertionFailed("notDeepStrictEqual", actual, expected, message);
    }
}

void ts_assert_throws(void* fn, void* error, void* message) {
    if (!fn) {
        assertionFailed("throws", nullptr, error, message);
        return;
    }

    // fn is a NaN-boxed TsValue* - pass directly to ts_function_call which handles NaN-boxed values
    TsValue fnVal = nanbox_to_tagged((TsValue*)fn);
    // Accept FUNCTION_PTR (raw function pointers) and OBJECT_PTR (closures/TsClosure)
    if ((fnVal.type != ValueType::FUNCTION_PTR && fnVal.type != ValueType::OBJECT_PTR) || !fnVal.ptr_val) {
        assertionFailed("throws", fn, error, message);
        return;
    }

    // Push exception handler and call the function
    void* handler = ts_push_exception_handler();
    jmp_buf* env = (jmp_buf*)handler;

    if (setjmp(*env) == 0) {
        // Try to call the function - pass NaN-boxed pointer
        ts_function_call((TsValue*)fn, 0, nullptr);
        // If we get here, no exception was thrown
        ts_pop_exception_handler();
        assertionFailed("throws", fn, error, message);
    } else {
        // Exception was caught - this is expected for throws()
        ts_pop_exception_handler();
        TsValue* caughtException = ts_get_exception();
        ts_set_exception(nullptr);  // Clear the exception

        // TODO: If error parameter is provided, verify exception matches
        // For now, just verify that something was thrown
        (void)caughtException;
        (void)error;
    }
}

void ts_assert_does_not_throw(void* fn, void* error, void* message) {
    if (!fn) return;

    TsValue fnVal = nanbox_to_tagged((TsValue*)fn);
    // Accept FUNCTION_PTR (raw function pointers) and OBJECT_PTR (closures/TsClosure)
    if ((fnVal.type != ValueType::FUNCTION_PTR && fnVal.type != ValueType::OBJECT_PTR) || !fnVal.ptr_val) {
        return;
    }

    // Push exception handler and call the function
    void* handler = ts_push_exception_handler();
    jmp_buf* env = (jmp_buf*)handler;

    if (setjmp(*env) == 0) {
        // Try to call the function - pass NaN-boxed pointer
        ts_function_call((TsValue*)fn, 0, nullptr);
        // If we get here, no exception was thrown - good!
        ts_pop_exception_handler();
    } else {
        // Exception was caught - this is a failure for doesNotThrow()
        ts_pop_exception_handler();
        TsValue* caughtException = ts_get_exception();
        ts_set_exception(nullptr);  // Clear the exception

        const char* msg = getMessageStr(message);
        fprintf(stderr, "AssertionError [ERR_ASSERTION]: Got unwanted exception");
        if (msg && strlen(msg) > 0) {
            fprintf(stderr, ": %s", msg);
        }
        fprintf(stderr, "\n");
        exit(1);
    }
}

// Handler for assert.rejects when promise resolves (FAILURE - should have rejected)
static TsValue* ts_assert_rejects_fulfilled_handler(void* context, TsValue* value) {
    AsyncAssertContext* ctx = (AsyncAssertContext*)context;
    // Promise resolved when it should have rejected - FAIL
    const char* msg = getMessageStr(ctx->assertMessage);
    fprintf(stderr, "AssertionError [ERR_ASSERTION]: Missing expected rejection");
    if (msg && strlen(msg) > 0) {
        fprintf(stderr, ": %s", msg);
    }
    fprintf(stderr, "\n");

    TsValue* err = (TsValue*)ts_error_create(TsString::Create("Missing expected rejection"));
    ts::ts_promise_reject_internal(ctx->resultPromise, err);
    return nullptr;
}

// Handler for assert.rejects when promise rejects (SUCCESS - expected behavior)
static TsValue* ts_assert_rejects_rejected_handler(void* context, TsValue* reason) {
    AsyncAssertContext* ctx = (AsyncAssertContext*)context;
    // Promise rejected as expected - SUCCESS
    // TODO: Optionally validate rejection reason matches ctx->expectedError
    ts::ts_promise_resolve_internal(ctx->resultPromise, ts_value_make_undefined());
    return nullptr;
}

void* ts_assert_rejects(void* asyncFn, void* error, void* message) {
    TsValue inputVal = nanbox_to_tagged((TsValue*)asyncFn);
    ts::TsPromise* inputPromise = nullptr;

    // 1. Get promise (call function if needed)
    if (inputVal.type == ValueType::PROMISE_PTR) {
        inputPromise = (ts::TsPromise*)inputVal.ptr_val;
    } else if (inputVal.type == ValueType::FUNCTION_PTR) {
        // Call the async function to get the promise
        TsValue* result = ts_function_call((TsValue*)asyncFn, 0, nullptr);
        TsValue resultVal = nanbox_to_tagged(result);
        if (resultVal.type == ValueType::PROMISE_PTR) {
            inputPromise = (ts::TsPromise*)resultVal.ptr_val;
        }
    }

    if (!inputPromise) {
        // Not a promise or async function - immediate failure
        ts::TsPromise* p = ts::ts_promise_create();
        TsValue* err = (TsValue*)ts_error_create(TsString::Create("Promise or async function required"));
        ts::ts_promise_reject_internal(p, err);
        return ts_value_make_promise(p);
    }

    // 2. Create result promise
    ts::TsPromise* resultPromise = ts::ts_promise_create();

    // 3. Create context and attach handlers
    AsyncAssertContext* ctx = (AsyncAssertContext*)ts_alloc(sizeof(AsyncAssertContext));
    ctx->resultPromise = resultPromise;
    ctx->expectedError = error;
    ctx->assertMessage = message;

    TsValue onFulfilled = nanbox_to_tagged(ts_value_make_function(
        (void*)ts_assert_rejects_fulfilled_handler, ctx));
    TsValue onRejected = nanbox_to_tagged(ts_value_make_function(
        (void*)ts_assert_rejects_rejected_handler, ctx));

    inputPromise->then(onFulfilled, onRejected);

    return ts_value_make_promise(resultPromise);
}

// Handler for assert.doesNotReject when promise resolves (SUCCESS - expected)
static TsValue* ts_assert_does_not_reject_fulfilled_handler(void* context, TsValue* value) {
    AsyncAssertContext* ctx = (AsyncAssertContext*)context;
    // Promise resolved as expected - SUCCESS
    ts::ts_promise_resolve_internal(ctx->resultPromise, ts_value_make_undefined());
    return nullptr;
}

// Handler for assert.doesNotReject when promise rejects (FAILURE - unexpected)
static TsValue* ts_assert_does_not_reject_rejected_handler(void* context, TsValue* reason) {
    AsyncAssertContext* ctx = (AsyncAssertContext*)context;
    // Promise rejected unexpectedly - FAIL
    const char* msg = getMessageStr(ctx->assertMessage);
    fprintf(stderr, "AssertionError [ERR_ASSERTION]: Got unwanted rejection");
    if (msg && strlen(msg) > 0) {
        fprintf(stderr, ": %s", msg);
    }
    fprintf(stderr, "\n");

    TsValue* err = (TsValue*)ts_error_create(TsString::Create("Got unwanted rejection"));
    ts::ts_promise_reject_internal(ctx->resultPromise, err);
    return nullptr;
}

void* ts_assert_does_not_reject(void* asyncFn, void* error, void* message) {
    TsValue inputVal = nanbox_to_tagged((TsValue*)asyncFn);
    ts::TsPromise* inputPromise = nullptr;

    // 1. Get promise (call function if needed)
    if (inputVal.type == ValueType::PROMISE_PTR) {
        inputPromise = (ts::TsPromise*)inputVal.ptr_val;
    } else if (inputVal.type == ValueType::FUNCTION_PTR) {
        // Call the async function to get the promise
        TsValue* result = ts_function_call((TsValue*)asyncFn, 0, nullptr);
        TsValue resultVal = nanbox_to_tagged(result);
        if (resultVal.type == ValueType::PROMISE_PTR) {
            inputPromise = (ts::TsPromise*)resultVal.ptr_val;
        }
    }

    if (!inputPromise) {
        // Not a promise - treat as success (nothing to reject)
        ts::TsPromise* p = ts::ts_promise_create();
        ts::ts_promise_resolve_internal(p, ts_value_make_undefined());
        return ts_value_make_promise(p);
    }

    // 2. Create result promise
    ts::TsPromise* resultPromise = ts::ts_promise_create();

    // 3. Create context and attach handlers
    AsyncAssertContext* ctx = (AsyncAssertContext*)ts_alloc(sizeof(AsyncAssertContext));
    ctx->resultPromise = resultPromise;
    ctx->expectedError = error;
    ctx->assertMessage = message;

    TsValue onFulfilled = nanbox_to_tagged(ts_value_make_function(
        (void*)ts_assert_does_not_reject_fulfilled_handler, ctx));
    TsValue onRejected = nanbox_to_tagged(ts_value_make_function(
        (void*)ts_assert_does_not_reject_rejected_handler, ctx));

    inputPromise->then(onFulfilled, onRejected);

    return ts_value_make_promise(resultPromise);
}

void ts_assert_fail(void* message) {
    const char* msg = getMessageStr(message);
    fprintf(stderr, "AssertionError [ERR_ASSERTION]: %s\n", msg ? msg : "Failed");
    exit(1);
}

void ts_assert_if_error(void* value) {
    if (isTruthy(value)) {
        fprintf(stderr, "AssertionError [ERR_ASSERTION]: ifError got unwanted exception\n");
        exit(1);
    }
}

void ts_assert_match(void* str, void* regexp, void* message) {
    TsValue strVal = nanbox_to_tagged((TsValue*)str);
    TsValue regexpVal = nanbox_to_tagged((TsValue*)regexp);

    if (strVal.type != ValueType::STRING_PTR) {
        assertionFailed("match", str, regexp, message);
        return;
    }

    TsString* tsStr = (TsString*)strVal.ptr_val;
    if (!tsStr) {
        assertionFailed("match", str, regexp, message);
        return;
    }

    // Get regexp object
    void* rawRegexp = regexpVal.ptr_val;
    if (!rawRegexp) {
        assertionFailed("match", str, regexp, message);
        return;
    }

    TsRegExp* re = (TsRegExp*)rawRegexp;
    void* matchResult = re->Exec(tsStr);

    if (!matchResult) {
        assertionFailed("match", str, regexp, message);
    }
}

void ts_assert_does_not_match(void* str, void* regexp, void* message) {
    TsValue strVal = nanbox_to_tagged((TsValue*)str);
    TsValue regexpVal = nanbox_to_tagged((TsValue*)regexp);

    if (strVal.type != ValueType::STRING_PTR) {
        return;  // No match on invalid input
    }

    TsString* tsStr = (TsString*)strVal.ptr_val;
    if (!tsStr) return;

    void* rawRegexp = regexpVal.ptr_val;
    if (!rawRegexp) return;

    TsRegExp* re = (TsRegExp*)rawRegexp;
    void* matchResult = re->Exec(tsStr);

    if (matchResult) {
        assertionFailed("doesNotMatch", str, regexp, message);
    }
}

} // extern "C"
