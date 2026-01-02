#include "TsObject.h"
#include "TsArray.h"
#include "TsBigInt.h"
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

    // Box any pointer by detecting its runtime type
    // This is used when the compile-time type is 'any' but we need proper boxing
    TsValue* ts_value_box_any(void* ptr) {
        if (!ptr) {
            return ts_value_make_undefined();
        }
        
        // Check if it's already a TsValue* (types 0-10)
        uint8_t firstByte = *(uint8_t*)ptr;
        if (firstByte <= 10) {
            // Looks like a TsValue type field - return as-is
            return (TsValue*)ptr;
        }
        
        // Check magic numbers to detect type
        uint32_t magic = *(uint32_t*)ptr;
        
        if (magic == 0x41525259) { // TsArray::MAGIC "ARRY"
            return ts_value_make_array(ptr);
        }
        if (magic == 0x4D415053) { // TsMap::MAGIC "MAPS"  
            return ts_value_make_object(ptr);
        }
        if (magic == 0x53455453) { // TsSet::MAGIC "SETS"
            return ts_value_make_object(ptr);
        }
        
        // Check for buffer magic at offset 8 (vtable is at 0)
        uint32_t magic8 = *(uint32_t*)((char*)ptr + 8);
        if (magic8 == 0x42554646) { // TsBuffer::MAGIC "BUFF"
            return ts_value_make_object(ptr);
        }
        
        // Default: treat as generic object
        return ts_value_make_object(ptr);
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

    // Strict equality comparison for boxed values (implements === semantics)
    bool ts_value_strict_eq(TsValue* lhs, TsValue* rhs) {
        if (!lhs && !rhs) return true;
        if (!lhs || !rhs) return false;
        
        // Get the actual TsValue structs (handle raw pointers)
        TsValue lhsVal = {};
        TsValue rhsVal = {};
        
        // Check if lhs is a raw pointer or a boxed TsValue
        uint8_t lhsType = *(uint8_t*)lhs;
        if (lhsType <= 10) {
            lhsVal = *lhs;
        } else {
            // It's a raw pointer - box it to determine type
            uint32_t magic = *(uint32_t*)lhs;
            if (magic == 0x41525259) { // TsArray::MAGIC
                lhsVal.type = ValueType::ARRAY_PTR;
                lhsVal.ptr_val = lhs;
            } else if (magic == 0x53545247) { // TsString::MAGIC
                lhsVal.type = ValueType::STRING_PTR;
                lhsVal.ptr_val = lhs;
            } else {
                lhsVal.type = ValueType::OBJECT_PTR;
                lhsVal.ptr_val = lhs;
            }
        }
        
        // Check if rhs is a raw pointer or a boxed TsValue
        uint8_t rhsType = *(uint8_t*)rhs;
        if (rhsType <= 10) {
            rhsVal = *rhs;
        } else {
            // It's a raw pointer - box it to determine type
            uint32_t magic = *(uint32_t*)rhs;
            if (magic == 0x41525259) { // TsArray::MAGIC
                rhsVal.type = ValueType::ARRAY_PTR;
                rhsVal.ptr_val = rhs;
            } else if (magic == 0x53545247) { // TsString::MAGIC
                rhsVal.type = ValueType::STRING_PTR;
                rhsVal.ptr_val = rhs;
            } else {
                rhsVal.type = ValueType::OBJECT_PTR;
                rhsVal.ptr_val = rhs;
            }
        }
        
        // Types must match for strict equality
        if (lhsVal.type != rhsVal.type) return false;
        
        switch (lhsVal.type) {
            case ValueType::UNDEFINED: return true;
            case ValueType::NUMBER_INT: return lhsVal.i_val == rhsVal.i_val;
            case ValueType::NUMBER_DBL: {
                if (std::isnan(lhsVal.d_val) && std::isnan(rhsVal.d_val)) return false; // NaN !== NaN
                return lhsVal.d_val == rhsVal.d_val;
            }
            case ValueType::BOOLEAN: return lhsVal.b_val == rhsVal.b_val;
            case ValueType::STRING_PTR: {
                TsString* s1 = (TsString*)lhsVal.ptr_val;
                TsString* s2 = (TsString*)rhsVal.ptr_val;
                if (!s1 && !s2) return true;
                if (!s1 || !s2) return false;
                return std::strcmp(s1->ToUtf8(), s2->ToUtf8()) == 0;
            }
            case ValueType::BIGINT_PTR: {
                TsBigInt* b1 = (TsBigInt*)lhsVal.ptr_val;
                TsBigInt* b2 = (TsBigInt*)rhsVal.ptr_val;
                if (!b1 && !b2) return true;
                if (!b1 || !b2) return false;
                return std::strcmp(b1->ToString(), b2->ToString()) == 0;
            }
            // For objects/arrays/etc, strict equality compares identity (same pointer)
            default: return lhsVal.ptr_val == rhsVal.ptr_val;
        }
    }

    // Note: ts_value_get_int and ts_value_get_double are defined in Primitives.cpp

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
        uint32_t magic20 = *(uint32_t*)((char*)obj + 20);
        uint32_t magic24 = *(uint32_t*)((char*)obj + 24);

        // Check for TsMap (magic at offset 16 after vtables) - also try offset 20 and 24
        if (magic16 == 0x4D415053 || magic20 == 0x4D415053 || magic24 == 0x4D415053) { // TsMap::MAGIC ("MAPS")
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

    // Object static methods
    
    // Object.keys(obj) - returns array of string keys
    void* ts_object_keys(void* obj) {
        if (!obj) return TsArray::Create(0);
        
        // Unbox if needed
        void* rawPtr = ts_value_get_object((TsValue*)obj);
        if (!rawPtr) rawPtr = obj;
        
        // Check TsMap::magic at offset 24
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 24);
        if (magic == 0x4D415053) { // TsMap::MAGIC
            return ts_map_keys(rawPtr);
        }
        
        // Not a map - return empty array
        return TsArray::Create(0);
    }
    
    // Object.values(obj) - returns array of values
    // Object.values(obj) - returns array of values
    void* ts_object_values(void* obj) {
        if (!obj) return TsArray::Create(0);
        
        void* rawPtr = ts_value_get_object((TsValue*)obj);
        if (!rawPtr) rawPtr = obj;
        
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 24);
        if (magic == 0x4D415053) { // TsMap::MAGIC
            return ts_map_values(rawPtr);
        }
        
        return TsArray::Create(0);
    }
    
    // Object.entries(obj) - returns array of [key, value] pairs
    void* ts_object_entries(void* obj) {
        if (!obj) return TsArray::Create(0);
        
        void* rawPtr = ts_value_get_object((TsValue*)obj);
        if (!rawPtr) rawPtr = obj;
        
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 24);
        if (magic == 0x4D415053) { // TsMap::MAGIC
            return ts_map_entries(rawPtr);
        }
        
        return TsArray::Create(0);
    }
    
    // Object.assign(target, source) - copies properties from source to target
    void* ts_object_assign(void* target, void* source) {
        if (!target) return target;
        if (!source) return target;
        
        void* targetRaw = ts_value_get_object((TsValue*)target);
        if (!targetRaw) targetRaw = target;
        
        void* sourceRaw = ts_value_get_object((TsValue*)source);
        if (!sourceRaw) sourceRaw = source;
        
        // Check both are TsMaps (magic at offset 24)
        uint32_t targetMagic = *(uint32_t*)((char*)targetRaw + 24);
        uint32_t sourceMagic = *(uint32_t*)((char*)sourceRaw + 24);
        
        if (targetMagic != 0x4D415053 || sourceMagic != 0x4D415053) {
            return target;
        }
        
        TsMap* targetMap = (TsMap*)targetRaw;
        TsMap* sourceMap = (TsMap*)sourceRaw;
        
        // Get entries from source and copy to target
        TsArray* entries = (TsArray*)sourceMap->GetEntries();
        int64_t len = entries->Length();
        for (int64_t i = 0; i < len; i++) {
            TsArray* entry = (TsArray*)entries->Get(i);
            TsValue* key = (TsValue*)entry->Get(0);
            TsValue* val = (TsValue*)entry->Get(1);
            targetMap->Set(*key, *val);
        }
        
        return target;
    }
    
    // Object.hasOwn(obj, prop) - check if object has own property
    bool ts_object_has_own(void* obj, void* prop) {
        if (!obj || !prop) return false;
        
        void* rawPtr = ts_value_get_object((TsValue*)obj);
        if (!rawPtr) rawPtr = obj;
        
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 24);
        if (magic == 0x4D415053) { // TsMap::MAGIC
            TsValue keyVal;
            keyVal.type = ValueType::STRING_PTR;
            keyVal.ptr_val = prop;
            return ts_map_has(rawPtr, &keyVal);
        }
        
        return false;
    }
    
    // Object.freeze(obj) - mark object as frozen (no-op for now, returns obj)
    void* ts_object_freeze(void* obj) {
        // TODO: Implement actual freezing when we have property descriptors
        return obj;
    }
    
    // Object.seal(obj) - mark object as sealed (no-op for now, returns obj)
    void* ts_object_seal(void* obj) {
        // TODO: Implement actual sealing when we have property descriptors
        return obj;
    }
    
    // Object.isFrozen(obj) - check if object is frozen
    bool ts_object_is_frozen(void* obj) {
        // TODO: Implement when we have property descriptors
        return false;
    }
    
    // Object.isSealed(obj) - check if object is sealed
    bool ts_object_is_sealed(void* obj) {
        // TODO: Implement when we have property descriptors
        return false;
    }
    
    // Object.fromEntries(iterable) - create object from key-value pairs
    void* ts_object_from_entries(void* entries) {
        TsMap* result = TsMap::Create();
        if (!entries) return result;
        
        void* rawPtr = ts_value_get_object((TsValue*)entries);
        if (!rawPtr) rawPtr = entries;
        
        // Check if it's an array
        uint32_t magic = *(uint32_t*)rawPtr;
        if (magic != 0x41525259) { // TsArray::MAGIC
            return result;
        }
        
        TsArray* arr = (TsArray*)rawPtr;
        int64_t len = arr->Length();
        
        for (int64_t i = 0; i < len; i++) {
            void* entry = (void*)arr->Get(i);
            if (!entry) continue;
            
            // Unbox entry if needed
            void* entryRaw = ts_value_get_object((TsValue*)entry);
            if (!entryRaw) entryRaw = entry;
            
            uint32_t entryMagic = *(uint32_t*)entryRaw;
            if (entryMagic != 0x41525259) continue;
            
            TsArray* pair = (TsArray*)entryRaw;
            if (pair->Length() < 2) continue;
            
            TsValue* key = (TsValue*)pair->Get(0);
            TsValue* val = (TsValue*)pair->Get(1);
            if (key && val) {
                result->Set(*key, *val);
            }
        }
        
        return result;
    }
}
