#pragma once
#include "TsEventEmitter.h"

class TsWritable : public virtual TsEventEmitter {
public:
    TsWritable() : closed(false), bufferedAmount(0), highWaterMark(16384), needDrain(false) {}
    virtual ~TsWritable() {}
    
    // Safe casting helper
    virtual TsWritable* AsWritable() override { return this; }

    virtual bool Write(void* data, size_t length) = 0;
    virtual void End() = 0;

protected:
    bool closed;
    size_t bufferedAmount;
    size_t highWaterMark;
    bool needDrain;
};
