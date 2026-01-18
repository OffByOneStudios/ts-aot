#pragma once

#include "TsMap.h"

// TsWeakMap is a wrapper around TsMap with its own MAGIC value
// Note: With Boehm GC, we cannot implement true weak reference semantics.
// This is effectively a regular Map but can be distinguished by isWeakMap().
class TsWeakMap : public TsMap {
public:
    static constexpr uint32_t MAGIC = 0x574D4150; // "WMAP"
    static TsWeakMap* Create();

private:
    TsWeakMap();
};
