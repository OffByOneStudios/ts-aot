#pragma once

#include <cstdint>
#include <cstddef>
#include "TsString.h"
#include "TsObject.h"

class TsBuffer : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x42554646; // "BUFF"
    static TsBuffer* Create(size_t length);
    static TsBuffer* FromString(TsString* str, TsString* encoding = nullptr);

    uint8_t* GetData() { return data; }
    size_t GetLength() { return length; }

    uint8_t Get(size_t index);
    void Set(size_t index, uint8_t value);

    TsString* ToString(TsString* encoding = nullptr);

private:
    TsBuffer(size_t length);
    uint32_t magic = MAGIC;
    uint8_t* data;
    size_t length;
};

extern "C" {
    void* ts_buffer_alloc(void* vtable, int64_t length);
    void* ts_buffer_from_string(void* vtable, void* str, void* encoding);
    int64_t ts_buffer_length(void* buf);
    uint8_t ts_buffer_get(void* buf, int64_t index);
    void ts_buffer_set(void* buf, int64_t index, uint8_t value);
    void* ts_buffer_to_string(void* buf, void* encoding);
}
