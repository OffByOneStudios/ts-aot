#pragma once

#include <cstdint>
#include <vector>
#include "TsString.h"
#include "TsObject.h"

class TsSet : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x53455453; // "SETS"
    static TsSet* Create();

    void Add(TsValue value);
    bool Has(TsValue value);
    bool Delete(TsValue value);
    void Clear();
    int64_t Size();
    void* GetValues();
    void ForEach(void* callback, void* thisArg = nullptr);

protected:
    TsSet();
private:
    uint32_t magic = MAGIC;
    void* impl; // Pointer to std::unordered_set
};

extern "C" {
    void* ts_set_create();
    void ts_set_add(void* set, TsValue* value);
    bool ts_set_has(void* set, TsValue* value);
    bool ts_set_delete(void* set, TsValue* value);
    void ts_set_clear(void* set);
    int64_t ts_set_size(void* set);
    void* ts_set_values(void* set);
    void ts_set_forEach(void* set, void* callback, void* thisArg);
}
