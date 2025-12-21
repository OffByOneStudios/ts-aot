#include "TsObject.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsString.h"
#include "GC.h"
#include "TsRuntime.h"
#include <new>
#include <cstdio>

// Currently empty, as TsObject.h only defines structs/enums for now.

extern "C" {

TsValue* ts_value_make_undefined() {
    TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
    v->type = ValueType::UNDEFINED;
    v->ptr_val = nullptr;
    return v;
}

extern "C" {
    TsValue* ts_value_make_int(int64_t i) {
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        v->type = ValueType::NUMBER_INT;
        v->i_val = i;
        return v;
    }

    TsValue* ts_value_make_double(double d) {
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        v->type = ValueType::NUMBER_DBL;
        v->d_val = d;
        return v;
    }

    TsValue* ts_value_make_bool(bool b) {
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        v->type = ValueType::BOOLEAN;
        v->b_val = b;
        return v;
    }

    TsValue* ts_value_make_string(void* s) {
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        v->type = ValueType::STRING_PTR;
        v->ptr_val = s;
        return v;
    }

    TsValue* ts_value_make_object(void* o) {
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        v->type = ValueType::OBJECT_PTR;
        v->ptr_val = o;
        return v;
    }

    TsValue* ts_value_make_promise(void* promise) {
        TsValue* val = (TsValue*)ts_alloc(sizeof(TsValue));
        val->type = ValueType::PROMISE_PTR;
        val->ptr_val = promise;
        return val;
    }

    TsValue* ts_value_make_array(void* arr) {
        TsValue* val = (TsValue*)ts_alloc(sizeof(TsValue));
        val->type = ValueType::ARRAY_PTR;
        val->ptr_val = arr;
        return val;
    }

    TsValue* ts_value_make_function(void* funcPtr, void* context) {
        TsFunction* func = new (ts_alloc(sizeof(TsFunction))) TsFunction(funcPtr, context);
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        v->type = ValueType::OBJECT_PTR;
        v->ptr_val = func;
        return v;
    }

    void* ts_function_get_ptr(TsValue* val) {
        if (!val || val->type != ValueType::OBJECT_PTR) return nullptr;
        TsFunction* func = (TsFunction*)val->ptr_val;
        return func->funcPtr;
    }

    int64_t ts_value_get_int(TsValue* v) {
        if (!v) return 0;
        if (v->type == ValueType::NUMBER_INT) return v->i_val;
        if (v->type == ValueType::NUMBER_DBL) return (int64_t)v->d_val;
        return 0;
    }

    double ts_value_get_double(TsValue* v) {
        if (!v) return 0.0;
        if (v->type == ValueType::NUMBER_DBL) return v->d_val;
        if (v->type == ValueType::NUMBER_INT) return (double)v->i_val;
        return 0.0;
    }

    bool ts_value_get_bool(TsValue* v) {
        if (!v) return false;
        if (v->type == ValueType::BOOLEAN) return v->b_val;
        return false;
    }

    void* ts_value_get_string(TsValue* v) {
        if (!v) return nullptr;
        if (v->type == ValueType::STRING_PTR) return v->ptr_val;
        return nullptr;
    }

    void* ts_value_get_object(TsValue* v) {
        if (!v) return nullptr;
        if (v->type == ValueType::OBJECT_PTR || v->type == ValueType::ARRAY_PTR || v->type == ValueType::PROMISE_PTR) return v->ptr_val;
        return nullptr;
    }

    int64_t ts_value_length(TsValue* val) {
        if (!val) return 0;
        if (val->type == ValueType::ARRAY_PTR) {
            return ts_array_length(val->ptr_val);
        }
        if (val->type == ValueType::STRING_PTR) {
            return ts_string_length(val->ptr_val);
        }
        return 0;
    }

    void* ts_value_get_element(TsValue* val, int64_t index) {
        if (!val) return nullptr;
        if (val->type == ValueType::ARRAY_PTR) {
            return ts_array_get(val->ptr_val, index);
        }
        return nullptr;
    }

    void* ts_object_get_property(void* obj, void* key) {
        if (!obj) return nullptr;
        
        // Try reading magic at offset 0 (for TsMap which doesn't have a vtable)
        uint32_t magic0 = *(uint32_t*)obj;
        if (magic0 == 0x4D415053) { // TsMap::MAGIC ("MAPS")
            TsValue val = ((TsMap*)obj)->Get((TsString*)key);
            TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
            *res = val;
            return res;
        }
        
        // Try reading magic at offset 8 (for TsObjects which have a vtable)
        uint32_t magic8 = *(uint32_t*)((char*)obj + sizeof(void*));
        if (magic8 == 0x48454144) { // TsHeaders::MAGIC ("HEAD")
            // TsHeaders layout: vtable (8), magic (4), padding (4), map (8)
            struct FakeHeaders {
                void* vtable;
                uint32_t magic;
                TsMap* map;
            };
            TsMap* map = ((FakeHeaders*)obj)->map;
            TsValue val = map->Get((TsString*)key);
            TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
            *res = val;
            return res;
        }
        return nullptr;
    }

    void* ts_value_get_property(TsValue* val, void* propName) {
        if (!val || !propName) return nullptr;
        
        TsString* key = nullptr;
        TsValue* propVal = (TsValue*)propName;
        if (propVal->type == ValueType::STRING_PTR) {
            key = (TsString*)propVal->ptr_val;
        } else {
            // TODO: Handle other types by converting to string
            return nullptr;
        }

        if (val->type == ValueType::OBJECT_PTR) {
            return ts_object_get_property(val->ptr_val, key);
        }
        return nullptr;
    }

    TsValue* ts_call_0(TsValue* boxedFunc) {
        if (!boxedFunc || boxedFunc->type != ValueType::OBJECT_PTR) return ts_value_make_undefined();
        TsFunction* func = (TsFunction*)boxedFunc->ptr_val;
        TsFunctionPtrNoArgs fn = (TsFunctionPtrNoArgs)func->funcPtr;
        return fn(func->context);
    }
}

} // extern "C"
