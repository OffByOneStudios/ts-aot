#pragma once

#include "TsMap.h"

// TsWeakMap is a wrapper around TsMap with its own MAGIC value
// Note: ts-aot's GC does not currently support weak references, so this is
// effectively a regular Map; it can be distinguished by isWeakMap().
class TsWeakMap : public TsMap {
public:
    static constexpr uint32_t MAGIC = 0x574D4150; // "WMAP"
    static TsWeakMap* Create();

private:
    TsWeakMap();
};
