#include "TsObject.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "TsEventEmitter.h"
#include "TsHttp.h"
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

TsValue* ts_value_make_null() {
    TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
    v->type = ValueType::UNDEFINED; // We don't have a NULL type yet, use UNDEFINED with nullptr
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
        // printf("ts_value_make_string(%p)\n", s);
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
        TsFunction* func = new (ts_alloc(sizeof(TsFunction))) TsFunction(funcPtr, context, FunctionType::COMPILED);
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        v->type = ValueType::OBJECT_PTR;
        v->ptr_val = func;
        return v;
    }

    TsValue* ts_value_make_native_function(void* funcPtr, void* context) {
        TsFunction* func = new (ts_alloc(sizeof(TsFunction))) TsFunction(funcPtr, context, FunctionType::NATIVE);
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
        return ts_value_to_bool(v);
    }

    void* ts_value_get_string(TsValue* v) {
        if (!v) return nullptr;
        
        // Check for raw TsString
        uint32_t magic = *(uint32_t*)v;
        if (magic == 0x53545247) { // TsString::MAGIC
            return v;
        }

        if (v->type == ValueType::STRING_PTR) return v->ptr_val;
        
        // If it's not a string, try to convert it (makes unboxing more robust)
        return ts_string_from_value(v);
    }

    void* ts_value_get_object(TsValue* v) {
        if (!v) {
            return nullptr;
        }

        // Check for raw pointers
        uint32_t magic = *(uint32_t*)v;
        if (magic == 0x41525259) { // TsArray::MAGIC
            return v;
        }
        if (magic == 0x4D415053) { // TsMap::MAGIC
            return v;
        }
        
        // Check for objects with vtable (magic at offset 8)
        uint32_t magic8 = *(uint32_t*)((char*)v + 8);
        if (magic8 == 0x42554646) { // TsBuffer::MAGIC
            return v;
        }

        if (v->type == ValueType::OBJECT_PTR || v->type == ValueType::ARRAY_PTR || v->type == ValueType::PROMISE_PTR) {
            return v->ptr_val;
        }
        return nullptr;
    }

    int64_t ts_value_length(TsValue* val) {
        if (!val) return 0;

        void* rawPtr = val;
        
        // Check if it's a boxed TsValue
        uint8_t type = *(uint8_t*)val;
        if (type <= 10) {
            if (type == (uint8_t)ValueType::STRING_PTR) return ((TsString*)val->ptr_val)->Length();
            if (type == (uint8_t)ValueType::ARRAY_PTR) return ((TsArray*)val->ptr_val)->Length();
            if (type == (uint8_t)ValueType::OBJECT_PTR) {
                rawPtr = val->ptr_val;
                if (!rawPtr) return 0;
            } else {
                return 0;
            }
        }

        uint32_t magic0 = *(uint32_t*)rawPtr;
        if (magic0 == 0x41525259) return ((TsArray*)rawPtr)->Length();
        if (magic0 == 0x53545247) return ((TsString*)rawPtr)->Length();

        uint32_t magic8 = *(uint32_t*)((char*)rawPtr + 8);
        if (magic8 == 0x42554646) {
            return ((TsBuffer*)rawPtr)->GetLength();
        }

        uint32_t magic16 = *(uint32_t*)((char*)rawPtr + 16);
        if (magic16 == 0x42554646) {
            return ((TsBuffer*)rawPtr)->GetLength();
        }

        return 0;
    }

    void* ts_value_get_element(void* param, int64_t index) {
        if (!param) return ts_value_make_undefined();

        TsValue* val = (TsValue*)param;
        void* rawPtr = param;
        
        // Unbox if it's a TsValue*
        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::ARRAY_PTR || val->type == ValueType::OBJECT_PTR) {
                rawPtr = val->ptr_val;
            } else if (val->type == ValueType::STRING_PTR) {
                TsString* s = (TsString*)val->ptr_val;
                return s->Substring(index, index + 1);
            } else if (val->type == ValueType::NUMBER_INT || val->type == ValueType::NUMBER_DBL || val->type == ValueType::BOOLEAN || val->type == ValueType::UNDEFINED) {
                return ts_value_make_undefined();
            }
        }

        if (!rawPtr) return ts_value_make_undefined();

        // Check magics
        uint32_t magic0 = *(uint32_t*)rawPtr;
        if (magic0 == 0x41525259) { // TsArray::MAGIC
            return ts_array_get(rawPtr, index);
        }

        uint32_t magic8 = *(uint32_t*)((char*)rawPtr + 8);
        if (magic8 == 0x42554646) { // TsBuffer::MAGIC
            TsBuffer* buf = (TsBuffer*)rawPtr;
            if (index < 0 || (size_t)index >= buf->GetLength()) {
                return ts_value_make_undefined();
            }
            return ts_value_make_int(buf->GetData()[index]);
        }

        uint32_t magic16 = *(uint32_t*)((char*)rawPtr + 16);
        if (magic16 == 0x42554646) { // TsBuffer::MAGIC
            TsBuffer* buf = (TsBuffer*)rawPtr;
            if (index < 0 || (size_t)index >= buf->GetLength()) {
                return ts_value_make_undefined();
            }
            return ts_value_make_int(buf->GetData()[index]);
        }

        return ts_value_make_undefined();
    }

    TsValue* ts_object_get_property(void* obj, const char* keyStr) {
        if (!obj) {
            return ts_value_make_undefined();
        }
        
        // Try dynamic_cast for TsIncomingMessage first (has virtual inheritance)
        TsObject* tsObj = (TsObject*)obj;
        TsIncomingMessage* incomingMsg = dynamic_cast<TsIncomingMessage*>(tsObj);
        if (incomingMsg) {
            if (strcmp(keyStr, "statusCode") == 0) {
                return ts_value_make_int(incomingMsg->statusCode);
            }
            if (strcmp(keyStr, "method") == 0) {
                return ts_value_make_string(incomingMsg->method);
            }
            if (strcmp(keyStr, "url") == 0) {
                return ts_value_make_string(incomingMsg->url);
            }
            if (strcmp(keyStr, "headers") == 0) {
                return ts_value_make_object(incomingMsg->headers);
            }
        }
        
        // TsObject layout: vtable (8) + TsObject::vtable member (8) = 16 bytes
        // TsMap adds: magic (4) + impl (8)
        // So magic is at offset 16 for TsMap, TsEventEmitter, etc.
        uint32_t magic0 = *(uint32_t*)obj;
        uint32_t magic8 = *(uint32_t*)((char*)obj + 8);
        uint32_t magic16 = *(uint32_t*)((char*)obj + 16);

        // Check for TsMap (magic at offset 16 after vtables)
        if (magic16 == 0x4D415053) { // TsMap::MAGIC ("MAPS")
            TsMap* map = (TsMap*)obj;
            TsValue k;
            k.type = ValueType::STRING_PTR;
            k.ptr_val = TsString::Create(keyStr);
            TsValue val = map->Get(k);
            TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
            *res = val;
            return res;
        }

        // 2. Fallback to magic-based checks for built-ins
        if (magic0 == 0x41525259 || magic8 == 0x41525259 || magic16 == 0x41525259) { // TsArray::MAGIC ("ARRY")
            if (strcmp(keyStr, "length") == 0) {
                return ts_value_make_int(((TsArray*)obj)->Length());
            }
        }
        if (magic0 == 0x53545247 || magic8 == 0x53545247 || magic16 == 0x53545247) { // TsString::MAGIC ("STRG")
            if (strcmp(keyStr, "length") == 0) {
                return ts_value_make_int(((TsString*)obj)->Length());
            }
        }
        if (magic0 == 0x42554646 || magic8 == 0x42554646 || magic16 == 0x42554646) { // TsBuffer::MAGIC ("BUFF")
            if (strcmp(keyStr, "length") == 0) {
                TsBuffer* buf = (TsBuffer*)obj;
                return ts_value_make_int(buf->GetLength());
            }
        }
        
        if (magic8 == 0x48454144 || magic16 == 0x48454144) { // TsHeaders::MAGIC ("HEAD")
            struct FakeHeaders { void* vtable; uint32_t magic; TsMap* map; };
            TsMap* map = ((FakeHeaders*)obj)->map;
            TsValue k; k.type = ValueType::STRING_PTR; k.ptr_val = TsString::Create(keyStr);
            TsValue val = map->Get(k);
            TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
            *res = val;
            return res;
        }
        if (magic8 == 0x45564E54 || magic16 == 0x45564E54) { // TsEventEmitter::MAGIC ("EVNT")
            if (strcmp(keyStr, "on") == 0) {
                return ts_value_make_function((void*)ts_event_emitter_on, obj);
            }
        }

        return ts_value_make_undefined();
    }

    TsValue* ts_value_get_property(TsValue* val, void* propName) {
        if (!val || !propName) {
            return nullptr;
        }
        
        const char* keyCStr = nullptr;
        // Check if propName is a raw TsString* or a TsValue*
        uint32_t magic = *(uint32_t*)propName;
        if (magic == 0x53545247) { // TsString::MAGIC
            keyCStr = ((TsString*)propName)->ToUtf8();
        } else {
            TsString* key = (TsString*)ts_value_get_string((TsValue*)propName);
            if (key) {
                keyCStr = key->ToUtf8();
            }
        }

        if (!keyCStr) {
            return ts_value_make_undefined();
        }

        if (val->type == ValueType::OBJECT_PTR || val->type == ValueType::ARRAY_PTR || val->type == ValueType::PROMISE_PTR) {
            return ts_object_get_property(val->ptr_val, keyCStr);
        }

        if (val->type == ValueType::STRING_PTR) {
            TsString* s = (TsString*)val->ptr_val;
            if (strcmp(keyCStr, "length") == 0) {
                return ts_value_make_int(s->Length());
            }
        }

        // Fallback for raw pointers (though they should be boxed)
        uint32_t magic0 = *(uint32_t*)val;
        uint32_t magic8 = *(uint32_t*)((char*)val + 8);
        if (magic0 == 0x41525259 || magic8 == 0x41525259 || // TsArray
            magic0 == 0x53545247 || magic8 == 0x53545247 || // TsString
            magic0 == 0x4D415053 || magic8 == 0x4D415053 || // TsMap
            magic0 == 0x45564E54 || magic8 == 0x45564E54) { // TsEventEmitter
            return ts_object_get_property(val, keyCStr);
        }

        return nullptr;
    }

    TsValue* ts_call_0(TsValue* boxedFunc) {
        if (!boxedFunc || boxedFunc->type != ValueType::OBJECT_PTR) return ts_value_make_undefined();
        TsFunction* func = (TsFunction*)boxedFunc->ptr_val;
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            return ((TsFunctionPtr)func->funcPtr)(func->context, 0, nullptr);
        } else {
            typedef TsValue* (*Fn0)(void*);
            TsValue* result = ((Fn0)func->funcPtr)(func->context);
            return result;
        }
    }

    TsValue* ts_call_1(TsValue* boxedFunc, TsValue* arg1) {
        if (!boxedFunc || boxedFunc->type != ValueType::OBJECT_PTR) {
            return ts_value_make_undefined();
        }
        TsFunction* func = (TsFunction*)boxedFunc->ptr_val;
        if (!func) {
            return ts_value_make_undefined();
        }
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[1] = { arg1 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 1, argv);
        } else {
            typedef TsValue* (*Fn1)(void*, TsValue*);
            return ((Fn1)func->funcPtr)(func->context, arg1);
        }
    }

    TsValue* ts_call_2(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2) {
        if (!boxedFunc) {
            return ts_value_make_undefined();
        }
        if (boxedFunc->type != ValueType::OBJECT_PTR) {
            return ts_value_make_undefined();
        }
        TsFunction* func = (TsFunction*)boxedFunc->ptr_val;
        if (!func) {
            return ts_value_make_undefined();
        }
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[2] = { arg1, arg2 };
            TsValue* result = ((TsFunctionPtr)func->funcPtr)(func->context, 2, argv);
            return result;
        } else {
            typedef TsValue* (*Fn2)(void*, TsValue*, TsValue*);
            TsValue* result = ((Fn2)func->funcPtr)(func->context, arg1, arg2);
            return result;
        }
    }

    TsValue* ts_call_3(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3) {
        if (!boxedFunc || boxedFunc->type != ValueType::OBJECT_PTR) return ts_value_make_undefined();
        TsFunction* func = (TsFunction*)boxedFunc->ptr_val;
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[3] = { arg1, arg2, arg3 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 3, argv);
        } else {
            typedef TsValue* (*Fn3)(void*, TsValue*, TsValue*, TsValue*);
            return ((Fn3)func->funcPtr)(func->context, arg1, arg2, arg3);
        }
    }

    TsValue* ts_function_call(TsValue* boxedFunc, int argc, TsValue** argv) {
        if (argc == 0) return ts_call_0(boxedFunc);
        if (argc == 1) return ts_call_1(boxedFunc, argv[0]);
        if (argc == 2) return ts_call_2(boxedFunc, argv[0], argv[1]);
        if (argc == 3) return ts_call_3(boxedFunc, argv[0], argv[1], argv[2]);
        // For now, we don't support more than 3 args in this helper
        return ts_value_make_undefined();
    }
}
