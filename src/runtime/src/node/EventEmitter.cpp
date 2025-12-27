#include "TsEventEmitter.h"
#include "TsString.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsRuntime.h"
#include <gc/gc.h>

TsEventEmitter::TsEventEmitter() {
    listeners = TsMap::Create();
}

TsEventEmitter::~TsEventEmitter() {
}

void TsEventEmitter::On(const char* event, void* callback) {
    TsValue eventVal;
    eventVal.type = ValueType::STRING_PTR;
    eventVal.ptr_val = TsString::Create(event);
    TsValue listenersVal = this->listeners->Get(eventVal);
    
    TsArray* arr;
    if (listenersVal.type == ValueType::UNDEFINED) {
        arr = TsArray::Create();
        this->listeners->Set(eventVal, TsValue(arr));
    } else {
        arr = (TsArray*)listenersVal.ptr_val;
    }
    arr->Push((int64_t)callback);
}

void TsEventEmitter::RemoveListener(const char* event, void* callback) {
    TsValue eventVal;
    eventVal.type = ValueType::STRING_PTR;
    eventVal.ptr_val = TsString::Create(event);
    TsValue listenersVal = this->listeners->Get(eventVal);
    
    if (listenersVal.type == ValueType::UNDEFINED) {
        return;
    }

    TsArray* arr = (TsArray*)listenersVal.ptr_val;
    TsValue* target = (TsValue*)callback;
    
    void* targetPtr = nullptr;
    void* targetCtx = nullptr;
    if (target && target->type == ValueType::OBJECT_PTR) {
        TsFunction* f = (TsFunction*)target->ptr_val;
        targetPtr = f->funcPtr;
        targetCtx = f->context;
    }

    for (int i = 0; i < arr->Length(); i++) {
        TsValue* existing = (TsValue*)arr->Get(i);
        if (existing == target) {
            arr->Remove((int64_t)existing);
            return;
        }
        
        if (existing && existing->type == ValueType::OBJECT_PTR && targetPtr) {
            TsFunction* f = (TsFunction*)existing->ptr_val;
            if (f->funcPtr == targetPtr && f->context == targetCtx) {
                arr->Remove((int64_t)existing);
                return;
            }
        }
    }
}

void TsEventEmitter::Emit(const char* event, int argc, void** argv) {
    TsValue eventVal;
    eventVal.type = ValueType::STRING_PTR;
    eventVal.ptr_val = TsString::Create(event);
    TsValue listenersVal = this->listeners->Get(eventVal);
    
    if (listenersVal.type == ValueType::UNDEFINED) {
        return;
    }

    TsArray* arr = (TsArray*)listenersVal.ptr_val;
    for (int i = 0; i < arr->Length(); i++) {
        TsValue* callback = (TsValue*)arr->Get(i);
        ts_function_call(callback, argc, (TsValue**)argv);
    }
}

int TsEventEmitter::ListenerCount(const char* event) {
    TsValue eventVal;
    eventVal.type = ValueType::STRING_PTR;
    eventVal.ptr_val = TsString::Create(event);
    TsValue listenersVal = this->listeners->Get(eventVal);
    
    if (listenersVal.type == ValueType::UNDEFINED) {
        return 0;
    }

    TsArray* arr = (TsArray*)listenersVal.ptr_val;
    return (int)arr->Length();
}

extern "C" {
    void ts_event_emitter_on(void* emitter, void* event, void* callback) {
        TsEventEmitter* e = (TsEventEmitter*)emitter;
        TsString* s = (TsString*)event;
        if (!s) {
            return;
        }
        e->On(s->ToUtf8(), callback);
    }
}
