#pragma once
#include "TsReadable.h"
#include "TsWritable.h"

class TsDuplex : public TsReadable, public TsWritable {
public:
    TsDuplex() {}
    virtual ~TsDuplex() {}
    
    // Safe casting helper
    virtual TsDuplex* AsDuplex() override { return this; }
};
