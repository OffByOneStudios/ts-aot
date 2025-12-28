#pragma once
#include "TsEventEmitter.h"

class TsReadable : public virtual TsEventEmitter {
public:
    TsReadable() : flowing(false), reading(false) {}
    virtual ~TsReadable() {}

    virtual void Pause() { flowing = false; }
    virtual void Resume() { flowing = true; }

protected:
    bool flowing;
    bool reading;
};
