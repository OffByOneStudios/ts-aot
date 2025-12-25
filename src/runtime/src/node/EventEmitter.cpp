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

void TsEventEmitter::Emit(const char* event, int argc, void** argv) {
    std::printf("Emitting event: %s\n", event);
    TsValue eventVal;
    eventVal.type = ValueType::STRING_PTR;
    eventVal.ptr_val = TsString::Create(event);
    TsValue listenersVal = this->listeners->Get(eventVal);
    
    if (listenersVal.type == ValueType::UNDEFINED) {
        std::printf("No listeners for event: %s\n", event);
        return;
    }

    TsArray* arr = (TsArray*)listenersVal.ptr_val;
    std::printf("Found %lld listeners for event: %s\n", arr->Length(), event);
    for (int i = 0; i < arr->Length(); i++) {
        TsValue* callback = (TsValue*)arr->Get(i);
        ts_function_call(callback, argc, (TsValue**)argv);
    }
}

extern "C" {
    void ts_event_emitter_on(void* emitter, void* event, void* callback) {
        TsEventEmitter* e = (TsEventEmitter*)emitter;
        TsString* s = (TsString*)event;
        e->On(s->ToUtf8(), callback);
    }
}
