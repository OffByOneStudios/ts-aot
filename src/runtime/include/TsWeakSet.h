#pragma once

#include "TsSet.h"

// TsWeakSet is a wrapper around TsSet with its own MAGIC value
// Note: ts-aot's GC does not currently support weak references, so this is
// effectively a regular Set; it can be distinguished by isWeakSet().
class TsWeakSet : public TsSet {
public:
    static constexpr uint32_t MAGIC = 0x57534554; // "WSET"
    static TsWeakSet* Create();

private:
    TsWeakSet();
};
