#pragma once
#include "TsEventEmitter.h"
#include "TsBuffer.h"

class TsDuplex : public TsEventEmitter {
public:
    TsDuplex() {}
    virtual ~TsDuplex() {}

    // Readable interface
    virtual void Pause() = 0;
    virtual void Resume() = 0;

    // Writable interface
    virtual bool Write(void* data, size_t length) = 0;
    virtual void End() = 0;
};
