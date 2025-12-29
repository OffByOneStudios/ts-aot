#pragma once
#include "TsObject.h"
#include "TsString.h"
#include "TsMap.h"
#include "TsArray.h"

/*
 * ⚠️ VIRTUAL INHERITANCE WARNING ⚠️
 * 
 * TsEventEmitter uses VIRTUAL INHERITANCE in derived classes (TsReadable, TsWritable, etc.)
 * 
 * NEVER cast pointers using C-style casts or pointer arithmetic!
 * ALWAYS use AsEventEmitter() or dynamic_cast<TsEventEmitter*>()
 * 
 * Example:
 *   TsEventEmitter* e = ((TsObject*)ptr)->AsEventEmitter();  // ✅ CORRECT
 *   TsEventEmitter* e = (TsEventEmitter*)ptr;                // ❌ BROKEN!
 * 
 * See .github/instructions/runtime-extensions.instructions.md for details.
 */

class TsEventEmitter : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x45564E54; // "EVNT"
    TsEventEmitter();
    virtual ~TsEventEmitter();
    
    virtual TsEventEmitter* AsEventEmitter() override { return this; }
    
    virtual void On(const char* event, void* callback);
    void Once(const char* event, void* callback);
    void PrependListener(const char* event, void* callback);
    void PrependOnceListener(const char* event, void* callback);
    void RemoveListener(const char* event, void* callback);
    void RemoveAllListeners(const char* event = nullptr);
    void Emit(const char* event, int argc, void** argv);
    int ListenerCount(const char* event);
    void SetMaxListeners(int n);
    int GetMaxListeners();

protected:
    TsMap* listeners;
    int maxListeners = 10;
};

extern "C" {
    void* ts_event_emitter_create();
    void ts_event_emitter_on(void* emitter, void* event, void* callback);
    void ts_event_emitter_once(void* emitter, void* event, void* callback);
    void ts_event_emitter_prepend_listener(void* emitter, void* event, void* callback);
    void ts_event_emitter_prepend_once_listener(void* emitter, void* event, void* callback);
    void ts_event_emitter_remove_all_listeners(void* emitter, void* event);
    void ts_event_emitter_set_max_listeners(void* emitter, int n);
    void ts_event_emitter_emit(void* emitter, void* event, int argc, void** argv);
}
