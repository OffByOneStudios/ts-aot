#pragma once

#include "TsSet.h"

// TsWeakSet is a wrapper around TsSet with its own MAGIC value
// Note: With Boehm GC, we cannot implement true weak reference semantics.
// This is effectively a regular Set but can be distinguished by isWeakSet().
class TsWeakSet : public TsSet {
public:
    static constexpr uint32_t MAGIC = 0x57534554; // "WSET"
    static TsWeakSet* Create();

private:
    TsWeakSet();
};
