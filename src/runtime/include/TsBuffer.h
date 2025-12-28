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

    uint32_t magic = MAGIC;

private:
    TsBuffer(size_t length);
    uint8_t* data;
    size_t length;
};

class TsTypedArray : public TsObject {
public:
    static TsTypedArray* Create(size_t length, size_t elementSize);
    uint8_t* GetData() { return buffer->GetData(); }
    size_t GetLength() { return length; }
    TsBuffer* GetBuffer() { return buffer; }

private:
    TsTypedArray(size_t length, size_t elementSize);
    TsBuffer* buffer;
    size_t length;
};

class TsDataView : public TsObject {
public:
    static TsDataView* Create(TsBuffer* buffer);
    TsBuffer* GetBuffer() { return buffer; }

private:
    TsDataView(TsBuffer* buffer);
    TsBuffer* buffer;
};

extern "C" {
    void* ts_buffer_alloc(int64_t length);
    void* ts_buffer_from(void* data);
    int64_t ts_buffer_length(void* buf);
    uint8_t ts_buffer_get(void* buf, int64_t index);
    void ts_buffer_set(void* buf, int64_t index, uint8_t value);
    void* ts_buffer_to_string(void* buf, void* encoding);

    void* ts_typed_array_create_u8(int64_t length);
    void* ts_typed_array_create_u32(int64_t length);
    void* ts_typed_array_create_f64(int64_t length);
    void* ts_typed_array_from_array_u8(void* array);
    void* ts_typed_array_from_array_u32(void* array);
    void* ts_typed_array_from_array_f64(void* array);
    void* ts_data_view_create(void* buffer);
    int64_t DataView_getUint32(void* context, void* dv, int64_t offset, bool littleEndian);
    void DataView_setUint32(void* context, void* dv, int64_t offset, int64_t value, bool littleEndian);
}
