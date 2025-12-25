#pragma once
#include "TsObject.h"
#include "TsString.h"
#include "TsMap.h"
#include "TsArray.h"

class TsEventEmitter : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x45564E54; // "EVNT"
    uint32_t magic = MAGIC;
    TsEventEmitter();
    virtual ~TsEventEmitter();
    
    void On(const char* event, void* callback);
    void Emit(const char* event, int argc, void** argv);

protected:
    TsMap* listeners;
};

extern "C" {
    void ts_event_emitter_on(void* emitter, void* event, void* callback);
}
