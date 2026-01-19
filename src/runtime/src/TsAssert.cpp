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

// Helper to get string value from TsValue
static const char* getMessageStr(void* message) {
    if (!message) return nullptr;
    TsValue* val = (TsValue*)message;
    if (val->type == ValueType::STRING_PTR) {
        TsString* str = (TsString*)val->ptr_val;
        return str ? str->ToUtf8() : nullptr;
    }
    // Try direct string pointer
    TsString* str = (TsString*)message;
    return str ? str->ToUtf8() : nullptr;
}

// Helper to check if value is truthy
static bool isTruthy(void* value) {
    if (!value) return false;
    TsValue* val = (TsValue*)value;

    switch (val->type) {
        case ValueType::NUMBER_INT:
            return val->i_val != 0;
        case ValueType::NUMBER_DBL:
            return val->d_val != 0.0;
        case ValueType::BOOLEAN:
            return val->b_val;
        case ValueType::UNDEFINED:
            return false;
        case ValueType::STRING_PTR: {
            TsString* str = (TsString*)val->ptr_val;
            return str && strlen(str->ToUtf8()) > 0;
        }
        case ValueType::OBJECT_PTR:
            return val->ptr_val != nullptr;
        default:
            return true;
    }
}

// Helper to check loose equality (==)
static bool looseEqual(void* a, void* b) {
    if (a == b) return true;
    if (!a || !b) return false;

    TsValue* va = (TsValue*)a;
    TsValue* vb = (TsValue*)b;

    // Both undefined
    if (va->type == ValueType::UNDEFINED && vb->type == ValueType::UNDEFINED) {
        return true;
    }

    // Numbers
    if (va->type == ValueType::NUMBER_INT && vb->type == ValueType::NUMBER_INT) {
        return va->i_val == vb->i_val;
    }
    if (va->type == ValueType::NUMBER_DBL && vb->type == ValueType::NUMBER_DBL) {
        return va->d_val == vb->d_val;
    }
    if ((va->type == ValueType::NUMBER_INT || va->type == ValueType::NUMBER_DBL) &&
        (vb->type == ValueType::NUMBER_INT || vb->type == ValueType::NUMBER_DBL)) {
        double da = va->type == ValueType::NUMBER_INT ? (double)va->i_val : va->d_val;
        double db = vb->type == ValueType::NUMBER_INT ? (double)vb->i_val : vb->d_val;
        return da == db;
    }

    // Booleans
    if (va->type == ValueType::BOOLEAN && vb->type == ValueType::BOOLEAN) {
        return va->b_val == vb->b_val;
    }

    // Strings
    if (va->type == ValueType::STRING_PTR && vb->type == ValueType::STRING_PTR) {
        TsString* sa = (TsString*)va->ptr_val;
        TsString* sb = (TsString*)vb->ptr_val;
        if (!sa || !sb) return sa == sb;
        return strcmp(sa->ToUtf8(), sb->ToUtf8()) == 0;
    }

    // Object identity
    if (va->type == ValueType::OBJECT_PTR && vb->type == ValueType::OBJECT_PTR) {
        return va->ptr_val == vb->ptr_val;
    }

    return false;
}

// Helper to check strict equality (===)
static bool strictEqual(void* a, void* b) {
    if (a == b) return true;
    if (!a || !b) return false;

    TsValue* va = (TsValue*)a;
    TsValue* vb = (TsValue*)b;

    // Must be same type for strict equality
    if (va->type != vb->type) return false;

    switch (va->type) {
        case ValueType::NUMBER_INT:
            return va->i_val == vb->i_val;
        case ValueType::NUMBER_DBL:
            return va->d_val == vb->d_val;
        case ValueType::BOOLEAN:
            return va->b_val == vb->b_val;
        case ValueType::UNDEFINED:
            return true;
        case ValueType::STRING_PTR: {
            TsString* sa = (TsString*)va->ptr_val;
            TsString* sb = (TsString*)vb->ptr_val;
            if (!sa || !sb) return sa == sb;
            return strcmp(sa->ToUtf8(), sb->ToUtf8()) == 0;
        }
        case ValueType::OBJECT_PTR:
            return va->ptr_val == vb->ptr_val;
        default:
            return false;
    }
}

// Forward declaration for deep comparison
static bool deepStrictEqual(void* a, void* b);

// Helper to check if a value looks like a pointer (high address)
static bool looksLikePointer(int64_t val) {
    // Pointers on 64-bit systems typically have high bits set
    // Small integers (like array values 1, 2, 3) have small absolute values
    // We consider anything > 0x10000 as potentially a pointer
    return val < 0 || val > 0x10000;
}

// Helper for deep array comparison
static bool deepEqualArrays(TsArray* a, TsArray* b) {
    if (a->Length() != b->Length()) return false;
    for (size_t i = 0; i < a->Length(); i++) {
        // Array elements are stored as int64_t which may be:
        // 1. Boxed TsValue* pointers
        // 2. Raw int64 values (for number arrays)
        int64_t va = a->Get(i);
        int64_t vb = b->Get(i);

        // First try direct integer comparison (for number arrays)
        if (va == vb) continue;

        // If neither looks like a pointer, treat as raw integers that differ
        if (!looksLikePointer(va) && !looksLikePointer(vb)) {
            return false;  // Different integer values
        }

        // Check if they might be boxed values (pointers)
        TsValue* valA = (TsValue*)va;
        TsValue* valB = (TsValue*)vb;

        // If both are non-zero and look like pointers, compare as boxed values
        if (va != 0 && vb != 0 && looksLikePointer(va) && looksLikePointer(vb)) {
            if (!deepStrictEqual(valA, valB)) {
                return false;
            }
        } else {
            return false;  // Type mismatch (one raw, one pointer)
        }
    }
    return true;
}

// Helper for deep object comparison
static bool deepEqualObjects(TsMap* a, TsMap* b) {
    // Get keys as arrays
    TsArray* keysA = (TsArray*)a->GetKeys();
    TsArray* keysB = (TsArray*)b->GetKeys();
    if (!keysA || !keysB) return keysA == keysB;
    if (keysA->Length() != keysB->Length()) return false;

    for (size_t i = 0; i < keysA->Length(); i++) {
        TsValue* key = (TsValue*)keysA->Get(i);
        if (!key) continue;
        TsValue valA = a->Get(*key);
        TsValue valB = b->Get(*key);
        if (!deepStrictEqual(&valA, &valB)) return false;
    }
    return true;
}

// Deep strict equality comparison
static bool deepStrictEqual(void* a, void* b) {
    if (a == b) return true;
    if (!a || !b) return a == b;

    TsValue* va = (TsValue*)a;
    TsValue* vb = (TsValue*)b;

    // Must be same type for strict equality
    if (va->type != vb->type) return false;

    switch (va->type) {
        case ValueType::NUMBER_INT:
            return va->i_val == vb->i_val;
        case ValueType::NUMBER_DBL:
            return va->d_val == vb->d_val;
        case ValueType::BOOLEAN:
            return va->b_val == vb->b_val;
        case ValueType::UNDEFINED:
            return true;
        case ValueType::STRING_PTR: {
            TsString* sa = (TsString*)va->ptr_val;
            TsString* sb = (TsString*)vb->ptr_val;
            if (!sa || !sb) return sa == sb;
            return strcmp(sa->ToUtf8(), sb->ToUtf8()) == 0;
        }
        case ValueType::ARRAY_PTR: {
            // Direct array type - compare as arrays
            TsArray* arrA = (TsArray*)va->ptr_val;
            TsArray* arrB = (TsArray*)vb->ptr_val;
            if (!arrA || !arrB) return arrA == arrB;
            return deepEqualArrays(arrA, arrB);
        }
        case ValueType::OBJECT_PTR: {
            TsObject* oa = (TsObject*)va->ptr_val;
            TsObject* ob = (TsObject*)vb->ptr_val;
            if (!oa || !ob) return oa == ob;

            // Check if both are arrays
            if (oa->magic == TsArray::MAGIC && ob->magic == TsArray::MAGIC) {
                return deepEqualArrays((TsArray*)oa, (TsArray*)ob);
            }

            // Check if both are maps/objects
            if (oa->magic == TsMap::MAGIC && ob->magic == TsMap::MAGIC) {
                return deepEqualObjects((TsMap*)oa, (TsMap*)ob);
            }

            // Different types or same reference
            return oa == ob;
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
    // Note: Since we don't have try/catch, we can't properly test for throws
    // This is a stub that warns about the limitation
    fprintf(stderr, "Warning: assert.throws() cannot verify exceptions in AOT-compiled code (no try/catch support)\n");
}

void ts_assert_does_not_throw(void* fn, void* error, void* message) {
    // Call the function - if it throws, the program will crash
    if (fn) {
        TsValue* fnVal = (TsValue*)fn;
        if (fnVal->type == ValueType::FUNCTION_PTR && fnVal->ptr_val) {
            ts_function_call(fnVal, 0, nullptr);
        }
    }
}

void* ts_assert_rejects(void* asyncFn, void* error, void* message) {
    // Stub - returns a resolved promise with warning
    fprintf(stderr, "Warning: assert.rejects() has limited support in AOT-compiled code\n");
    TsValue undefined = {};
    undefined.type = ValueType::UNDEFINED;
    return ts::ts_promise_resolve(nullptr, &undefined);
}

void* ts_assert_does_not_reject(void* asyncFn, void* error, void* message) {
    // Stub - returns the promise result
    fprintf(stderr, "Warning: assert.doesNotReject() has limited support in AOT-compiled code\n");
    TsValue undefined = {};
    undefined.type = ValueType::UNDEFINED;
    return ts::ts_promise_resolve(nullptr, &undefined);
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
    TsValue* strVal = (TsValue*)str;
    TsValue* regexpVal = (TsValue*)regexp;

    if (!strVal || strVal->type != ValueType::STRING_PTR) {
        assertionFailed("match", str, regexp, message);
        return;
    }

    TsString* tsStr = (TsString*)strVal->ptr_val;
    if (!tsStr) {
        assertionFailed("match", str, regexp, message);
        return;
    }

    // Get regexp object
    void* rawRegexp = regexpVal ? regexpVal->ptr_val : nullptr;
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
    TsValue* strVal = (TsValue*)str;
    TsValue* regexpVal = (TsValue*)regexp;

    if (!strVal || strVal->type != ValueType::STRING_PTR) {
        return;  // No match on invalid input
    }

    TsString* tsStr = (TsString*)strVal->ptr_val;
    if (!tsStr) return;

    void* rawRegexp = regexpVal ? regexpVal->ptr_val : nullptr;
    if (!rawRegexp) return;

    TsRegExp* re = (TsRegExp*)rawRegexp;
    void* matchResult = re->Exec(tsStr);

    if (matchResult) {
        assertionFailed("doesNotMatch", str, regexp, message);
    }
}

} // extern "C"
