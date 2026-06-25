#include "TsObject_Internal.h"

// Value boxing constructors (ts_value_make_*), the boxed-any detector
// (ts_value_box_any), and the Test262 IsHTMLDDA exotic object. Extracted from
// TsObject.cpp; shared state/types come via TsObject_Internal.h.
extern "C" {


TsValue* ts_value_make_undefined() {
    return nanbox_to_tsvalue_ptr(NANBOX_UNDEFINED);
}

TsValue* ts_value_make_null() {
    return nanbox_to_tsvalue_ptr(NANBOX_NULL);
}

TsValue* ts_value_make_int(int64_t i) {
        // Encode as int32 if it fits, otherwise as double (matches JS semantics)
        if (i >= INT32_MIN && i <= INT32_MAX) {
            return nanbox_to_tsvalue_ptr(nanbox_int32((int32_t)i));
        }
        return nanbox_to_tsvalue_ptr(nanbox_double((double)i));
    }

    TsValue* ts_value_make_double(double d) {
        return nanbox_to_tsvalue_ptr(nanbox_double(d));
    }

    TsValue* ts_value_make_bool(bool b) {
        return nanbox_to_tsvalue_ptr(nanbox_bool(b));
    }

    TsValue* ts_value_make_string(void* s) {
        if (!s) return nanbox_to_tsvalue_ptr(NANBOX_UNDEFINED);
        // With NaN boxing, a pointer IS its own encoding.
        // Check for NaN-boxed non-pointer values passed by mistake.
        uint64_t nb = (uint64_t)(uintptr_t)s;
        if (nanbox_is_number(nb) || nanbox_is_special(nb)) {
            return (TsValue*)s;  // Already NaN-boxed, return as-is
        }
        // It's a raw pointer (TsString* etc.) - NaN-boxed pointer IS the raw pointer
        return (TsValue*)s;
    }

    TsValue* ts_value_make_object(void* o) {
        if (!o) return nanbox_to_tsvalue_ptr(NANBOX_NULL);
        // With NaN boxing, a pointer IS its own encoding.
        // Check for NaN-boxed non-pointer values passed by mistake.
        uint64_t nb = (uint64_t)(uintptr_t)o;
        if (nanbox_is_number(nb) || nanbox_is_special(nb)) {
            return (TsValue*)o;  // Already NaN-boxed, return as-is
        }
        return (TsValue*)o;
    }

    TsValue* ts_value_make_function_object(void* fnObj) {
        if (!fnObj) return nanbox_to_tsvalue_ptr(NANBOX_UNDEFINED);
        return (TsValue*)fnObj;
    }

    TsValue* ts_value_make_promise(void* promise) {
        if (!promise) return nanbox_to_tsvalue_ptr(NANBOX_UNDEFINED);
        return (TsValue*)promise;
    }

    TsValue* ts_value_make_array(void* arr) {
        if (!arr) return nanbox_to_tsvalue_ptr(NANBOX_UNDEFINED);
        return (TsValue*)arr;
    }

    TsValue* ts_ensure_boxed(void* v) {
        if (!v) return nanbox_to_tsvalue_ptr(NANBOX_UNDEFINED);
        // With NaN boxing, check if it's already a NaN-boxed value
        uint64_t nb = (uint64_t)(uintptr_t)v;
        if (nanbox_is_number(nb) || nanbox_is_special(nb)) {
            return (TsValue*)v;  // Already NaN-boxed non-pointer
        }
        // It's a raw pointer - return as NaN-boxed pointer
        return (TsValue*)v;
    }

    bool ts_value_is_undefined(TsValue* v) {
        if (!v) return true;  // C++ nullptr treated as undefined
        uint64_t nb = nanbox_from_tsvalue_ptr(v);
        return nanbox_is_undefined(nb);
    }

    bool ts_value_is_null(TsValue* v) {
        if (!v) return false;  // C++ nullptr is not JavaScript null
        uint64_t nb = nanbox_from_tsvalue_ptr(v);
        return nanbox_is_null(nb);
    }

    // Box any pointer by detecting its runtime type
    // This is used when the compile-time type is 'any' but we need proper boxing
    TsValue* ts_value_box_any(void* ptr) {
        if (!ptr) {
            return ts_value_make_undefined();
        }

        // With NaN boxing, check if it's already a NaN-boxed value
        uint64_t nb = (uint64_t)(uintptr_t)ptr;
        if (nanbox_is_number(nb) || nanbox_is_special(nb)) {
            return (TsValue*)ptr;  // Already NaN-boxed non-pointer
        }

        // Guard against obviously-invalid pointers
        if (nb < 0x10000) {
            return ts_value_make_undefined();
        }

#ifdef _MSC_VER
        __try {
#endif

        // Check magic at canonical offsets only: POD types (Array/String) carry
        // magic at offset 0; TsObject subclasses (Function) at offset 16. The
        // former multi-offset scan (0/8/16/24) was off-by-N layout-uncertainty
        // tolerance, now proven vestigial (off-by-8 tripwire cold across the
        // full suite + GC stress). MAPS/SETS/BUFF branches were dropped: they
        // returned ts_value_make_object — identical to the default fallthrough.
        uint32_t magic = *(uint32_t*)ptr;                          // offset 0
        uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);          // offset 16

        if (magic == 0x41525259) { // TsArray::MAGIC "ARRY"
            return ts_value_make_array(ptr);
        }
        if (magic == 0x53545247 || magic == TsConsString::MAGIC) { // TsString or TsConsString
            return ts_value_make_string(ts_ensure_flat(ptr));
        }
        if (magic16 == 0x46554E43) { // TsFunction::MAGIC "FUNC"
            return ts_value_make_function_object(ptr);
        }

        // Tripwire: would the dropped off-canonical tolerance have classified an
        // Array or Function here (i.e. a misaligned receiver)? If this ever
        // fires, the tolerance was NOT vestigial — revert. (Only ARRY/FUNC
        // mattered; MAPS/SETS/BUFF collapse to the generic-object default.)
        {
            uint32_t magic8 = *(uint32_t*)((char*)ptr + 8);
            uint32_t magic24 = *(uint32_t*)((char*)ptr + 24);
            if (magic8 == 0x41525259 || magic16 == 0x41525259 ||      // ARRY off-canon
                magic8 == 0x46554E43 || magic == 0x46554E43 || magic24 == 0x46554E43) // FUNC off-canon
                ts_offcanon_note("ts_value_get_type", ptr);
        }

        // Default: treat as generic object (covers Map/Set/Buffer/unknown).
        return ts_value_make_object(ptr);

#ifdef _MSC_VER
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return ts_value_make_undefined();
        }
#endif
    }

    TsValue* ts_value_make_function(void* funcPtr, void* context) {
        TsFunction* func = new (ts_alloc(sizeof(TsFunction))) TsFunction(funcPtr, context, FunctionType::COMPILED, -1);
        return (TsValue*)func;
    }

    TsValue* ts_value_make_function_with_arity(void* funcPtr, void* context, int arity) {
        TsFunction* func = new (ts_alloc(sizeof(TsFunction))) TsFunction(funcPtr, context, FunctionType::COMPILED, arity);
        return (TsValue*)func;
    }

    TsValue* ts_value_make_function_named(void* funcPtr, void* context, void* name) {
        TsFunction* func = new (ts_alloc(sizeof(TsFunction))) TsFunction(funcPtr, context, FunctionType::COMPILED, -1);
        func->name = (TsString*)name;
        return (TsValue*)func;
    }

    TsValue* ts_value_make_native_function(void* funcPtr, void* context) {
        void* mem = ts_alloc(sizeof(TsFunction));
        TsFunction* func = new (mem) TsFunction(funcPtr, context, FunctionType::NATIVE);
        func->magic = TsFunction::MAGIC;
        return (TsValue*)func;
    }

    // Test262 host-defined exotic object with [[IsHTMLDDA]] internal slot.
    // Per Annex B, this object loose-equals null/undefined, ToBoolean returns
    // false, calling it returns undefined, typeof returns "undefined". The
    // ~3000-test "*-emulates-undefined" cluster across destructuring,
    // generators, TypedArray, and Symbol.iterator depends on this behavior.
    static TsValue* htmldda_call_native(void* /*ctx*/, int /*argc*/, TsValue** /*argv*/) {
        return ts_value_make_undefined();
    }
    TsValue* ts_create_htmldda() {
        void* mem = ts_alloc(sizeof(TsFunction));
        TsFunction* func = new (mem) TsFunction((void*)htmldda_call_native, nullptr,
                                                 FunctionType::NATIVE);
        func->magic = TsFunction::MAGIC;
        func->is_constructor = false;
        func->is_htmldda = true;
        func->name = TsString::Create("IsHTMLDDA");
        return (TsValue*)func;
    }

    // Returns true if val is a TsFunction with [[IsHTMLDDA]] = true.
    bool ts_is_htmldda(TsValue* val) {
        if (!val) return false;
        uint64_t nb = (uint64_t)(uintptr_t)val;
        if (!nanbox_is_ptr(nb) || nb <= NANBOX_UNDEFINED) return false;
        void* raw = ts_value_get_object(val);
        if (!raw) return false;
        uint32_t magic = *(uint32_t*)((char*)raw + 16);
        if (magic != TsFunction::MAGIC) return false;
        return ((TsFunction*)raw)->is_htmldda;
    }

}  // extern "C"
