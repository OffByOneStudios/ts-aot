#include "TsObject.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "TsEventEmitter.h"
#include "GC.h"
#include "TsRuntime.h"
#include <new>
#include <cstdio>
#include <iostream>

// Currently empty, as TsObject.h only defines structs/enums for now.

extern "C" {

TsValue* ts_value_make_undefined() {
    TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
    v->type = ValueType::UNDEFINED;
    v->ptr_val = nullptr;
    return v;
}

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

        // Check for raw pointers
        uint32_t magic = *(uint32_t*)val;
        if (magic == 0x41525259) { // TsArray::MAGIC
            return ((TsArray*)val)->Length();
        }
        if (magic == 0x53545247) { // TsString::MAGIC
            return ((TsString*)val)->Length();
        }

        // Check for TsBuffer (magic at offset 8 due to vtable)
        // We need to be careful not to read out of bounds if it's a small TsValue
        // But TsValue is at least 16 bytes (enum + union), so offset 8 is safe.
        uint32_t magic8 = *(uint32_t*)((char*)val + 8);
        if (magic8 == 0x42554646) { // TsBuffer::MAGIC
            return ((TsBuffer*)val)->GetLength();
        }

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

        uint32_t magic = *(uint32_t*)val;
        if (magic == 0x41525259) { // TsArray::MAGIC
            return ts_array_get(val, index);
        }

        if (val->type == ValueType::ARRAY_PTR) {
            return ts_array_get(val->ptr_val, index);
        }
        if (val->type == ValueType::STRING_PTR) {
            // Return a single character string
            TsString* s = (TsString*)val->ptr_val;
            return s->Substring(index, index + 1);
        }
        return nullptr;
    }

    void* ts_object_get_property(void* obj, void* key) {
        if (!obj) return nullptr;
        
        TsString* tsKey = (TsString*)key;

        // Try reading magic at offset 0 (for objects without vtable)
        uint32_t magic0 = *(uint32_t*)obj;
        uint32_t magic8 = *(uint32_t*)((char*)obj + sizeof(void*));

        if (magic0 == 0x4D415053) { // TsMap::MAGIC ("MAPS")
            TsValue val = ((TsMap*)obj)->Get(tsKey);
            TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
            *res = val;
            return res;
        }
        if (magic0 == 0x41525259) { // TsArray::MAGIC ("ARRY")
            if (strcmp(tsKey->ToUtf8(), "length") == 0) {
                TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
                res->type = ValueType::NUMBER_INT;
                res->i_val = ((TsArray*)obj)->Length();
                return res;
            }
            return nullptr;
        }
        if (magic0 == 0x53545247) { // TsString::MAGIC ("STRG")
            if (strcmp(tsKey->ToUtf8(), "length") == 0) {
                TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
                res->type = ValueType::NUMBER_INT;
                res->i_val = ((TsString*)obj)->Length();
                return res;
            }
            return nullptr;
        }
        
        // Try reading magic at offset 8 (for TsObjects which have a vtable)
        if (magic8 == 0x4D415053) { // TsMap::MAGIC ("MAPS")
            TsValue val = ((TsMap*)obj)->Get(tsKey);
            TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
            *res = val;
            return res;
        }
        if (magic8 == 0x48454144) { // TsHeaders::MAGIC ("HEAD")
            // TsHeaders layout: vtable (8), magic (4), padding (4), map (8)
            struct FakeHeaders {
                void* vtable;
                uint32_t magic;
                TsMap* map;
            };
            TsMap* map = ((FakeHeaders*)obj)->map;
            TsValue val = map->Get(tsKey);
            TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
            *res = val;
            return res;
        }
        if (magic8 == 0x42554646) { // TsBuffer::MAGIC ("BUFF")
            if (strcmp(tsKey->ToUtf8(), "length") == 0) {
                TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
                res->type = ValueType::NUMBER_INT;
                res->i_val = ((TsBuffer*)obj)->GetLength();
                return res;
            }
            return nullptr;
        }
        if (magic8 == 0x45564E54) { // TsEventEmitter::MAGIC ("EVNT")
            if (strcmp(tsKey->ToUtf8(), "on") == 0) {
                return ts_value_make_function((void*)ts_event_emitter_on, obj);
            }
            return nullptr;
        }

        // If no magic matches, assume it's a generated class with __get_property at vtable[1]
        void** vtable = *(void***)obj;
        if (vtable) {
            typedef TsValue* (*GetPropertyFn)(void*, void*);
            GetPropertyFn getProp = (GetPropertyFn)vtable[1];
            if (getProp) {
                return getProp(obj, key);
            }
        }
        return nullptr;
    }

    void* ts_value_get_property(TsValue* val, void* propName) {
        if (!val || !propName) return nullptr;
        
        TsString* key = (TsString*)propName;

        uint32_t magic = *(uint32_t*)val;

        if (magic == 0x41525259 || magic == 0x53545247 || magic == 0x4D415053 || magic == 0x45564E54) {
            return ts_object_get_property(val, key);
        }

        if (val->type == ValueType::OBJECT_PTR || val->type == ValueType::ARRAY_PTR || val->type == ValueType::PROMISE_PTR) {
            void* ret = ts_object_get_property(val->ptr_val, key);
            return ret;
        }
        return nullptr;
    }

    TsValue* ts_call_0(TsValue* boxedFunc) {
        if (!boxedFunc || boxedFunc->type != ValueType::OBJECT_PTR) return ts_value_make_undefined();
        TsFunction* func = (TsFunction*)boxedFunc->ptr_val;
        TsFunctionPtrNoArgs fn = (TsFunctionPtrNoArgs)func->funcPtr;
        return fn(func->context);
    }

    TsValue* ts_call_1(TsValue* boxedFunc, TsValue* arg1) {
        if (!boxedFunc) {
            printf("ts_call_1: boxedFunc is null\n");
            return ts_value_make_undefined();
        }
        if (boxedFunc->type != ValueType::OBJECT_PTR) {
            uint64_t val = *(uint64_t*)boxedFunc;
            printf("ts_call_1: boxedFunc %p type is %d, expected %d. First 8 bytes: %llx\n", boxedFunc, (int)boxedFunc->type, (int)ValueType::OBJECT_PTR, val);
            return ts_value_make_undefined();
        }
        TsFunction* func = (TsFunction*)boxedFunc->ptr_val;
        if (!func) {
            printf("ts_call_1: func is null\n");
            return ts_value_make_undefined();
        }
        typedef TsValue* (*Fn1)(void*, TsValue*);
        Fn1 fn = (Fn1)func->funcPtr;
        return fn(func->context, arg1);
    }

    TsValue* ts_call_2(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2) {
        if (!boxedFunc || boxedFunc->type != ValueType::OBJECT_PTR) return ts_value_make_undefined();
        TsFunction* func = (TsFunction*)boxedFunc->ptr_val;
        typedef TsValue* (*Fn2)(void*, TsValue*, TsValue*);
        Fn2 fn = (Fn2)func->funcPtr;
        return fn(func->context, arg1, arg2);
    }

    TsValue* ts_function_call(TsValue* boxedFunc, int argc, TsValue** argv) {
        if (argc == 0) return ts_call_0(boxedFunc);
        if (argc == 1) return ts_call_1(boxedFunc, argv[0]);
        if (argc == 2) return ts_call_2(boxedFunc, argv[0], argv[1]);
        // For now, we don't support more than 2 args in this helper
        return ts_value_make_undefined();
    }
}
