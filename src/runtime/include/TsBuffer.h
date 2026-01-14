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
    static TsBuffer* FromHex(TsString* hexStr);
    static TsBuffer* FromBase64(TsString* b64Str);

    uint8_t* GetData() { return data; }
    size_t GetLength() { return length; }

    // TypedArray-like properties
    size_t GetByteLength() { return length; }
    size_t GetByteOffset() { return 0; }  // Buffer always starts at offset 0
    TsBuffer* GetArrayBuffer() { return this; }  // For Node.js Buffer, buffer.buffer returns the same buffer

    uint8_t Get(size_t index);
    void Set(size_t index, uint8_t value);

    TsString* ToString(TsString* encoding = nullptr);
    TsString* ToHex();
    TsString* ToBase64();
    TsString* ToBase64Url();

    // New Buffer API methods
    TsBuffer* Slice(int64_t start, int64_t end);
    TsBuffer* Subarray(int64_t start, int64_t end);
    void Fill(uint8_t value, int64_t start = 0, int64_t end = -1);
    int64_t Copy(TsBuffer* target, int64_t targetStart = 0, int64_t sourceStart = 0, int64_t sourceEnd = -1);

    // Read methods
    int8_t ReadInt8(size_t offset);
    uint8_t ReadUInt8(size_t offset);
    int16_t ReadInt16LE(size_t offset);
    int16_t ReadInt16BE(size_t offset);
    uint16_t ReadUInt16LE(size_t offset);
    uint16_t ReadUInt16BE(size_t offset);
    int32_t ReadInt32LE(size_t offset);
    int32_t ReadInt32BE(size_t offset);
    uint32_t ReadUInt32LE(size_t offset);
    uint32_t ReadUInt32BE(size_t offset);
    float ReadFloatLE(size_t offset);
    float ReadFloatBE(size_t offset);
    double ReadDoubleLE(size_t offset);
    double ReadDoubleBE(size_t offset);

    // Write methods - return offset + bytes written
    size_t WriteInt8(int8_t value, size_t offset);
    size_t WriteUInt8(uint8_t value, size_t offset);
    size_t WriteInt16LE(int16_t value, size_t offset);
    size_t WriteInt16BE(int16_t value, size_t offset);
    size_t WriteUInt16LE(uint16_t value, size_t offset);
    size_t WriteUInt16BE(uint16_t value, size_t offset);
    size_t WriteInt32LE(int32_t value, size_t offset);
    size_t WriteInt32BE(int32_t value, size_t offset);
    size_t WriteUInt32LE(uint32_t value, size_t offset);
    size_t WriteUInt32BE(uint32_t value, size_t offset);
    size_t WriteFloatLE(float value, size_t offset);
    size_t WriteFloatBE(float value, size_t offset);
    size_t WriteDoubleLE(double value, size_t offset);
    size_t WriteDoubleBE(double value, size_t offset);

    // Utility methods
    int Compare(TsBuffer* other);
    bool Equals(TsBuffer* other);
    int64_t IndexOf(uint8_t value, size_t byteOffset = 0);
    int64_t IndexOfBuffer(TsBuffer* value, size_t byteOffset = 0);
    int64_t LastIndexOf(uint8_t value, size_t byteOffset = SIZE_MAX);
    bool Includes(uint8_t value, size_t byteOffset = 0);
    bool IncludesBuffer(TsBuffer* value, size_t byteOffset = 0);

    // Static methods
    static TsBuffer* AllocUnsafe(size_t length);
    static TsBuffer* Concat(void* list, int64_t totalLength = -1);
    static int Compare(TsBuffer* buf1, TsBuffer* buf2);
    static bool IsEncoding(const char* encoding);

private:
    TsBuffer(size_t length);
    uint8_t* data;
    size_t length;
};

class TsTypedArray : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x54415252; // "TARR"
    static TsTypedArray* Create(size_t length, size_t elementSize, bool clamped = false);
    uint8_t* GetData() { return buffer->GetData(); }
    size_t GetLength() { return length; }
    size_t GetByteLength() { return length * elementSize; }
    size_t GetByteOffset() { return 0; }  // TypedArrays start at offset 0
    size_t GetElementSize() { return elementSize; }
    bool IsClamped() { return clamped; }
    TsBuffer* GetBuffer() { return buffer; }

    // Element access
    double Get(size_t index);
    void Set(size_t index, double value);

private:
    TsTypedArray(size_t length, size_t elementSize, bool clamped);
    // Note: magic is inherited from TsObject and set in constructor
    TsBuffer* buffer;
    size_t length;
    size_t elementSize;
    bool clamped;
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
    void* ts_buffer_alloc_unsafe(int64_t length);
    void* ts_buffer_from(void* data);
    void* ts_buffer_from_string(void* str, void* encoding);
    void* ts_buffer_concat(void* list, int64_t totalLength);
    int64_t ts_buffer_length(void* buf);
    int64_t ts_buffer_byte_length(void* buf);
    int64_t ts_buffer_byte_offset(void* buf);
    void* ts_buffer_get_array_buffer(void* buf);
    uint8_t ts_buffer_get(void* buf, int64_t index);
    void ts_buffer_set(void* buf, int64_t index, uint8_t value);
    void* ts_buffer_to_string(void* buf, void* encoding);
    void* ts_buffer_slice(void* buf, int64_t start, int64_t end);
    void* ts_buffer_subarray(void* buf, int64_t start, int64_t end);
    void* ts_buffer_fill(void* buf, int64_t value, int64_t start, int64_t end);
    int64_t ts_buffer_copy(void* source, void* target, int64_t targetStart, int64_t sourceStart, int64_t sourceEnd);
    bool ts_buffer_is_buffer(void* obj);
    bool ts_buffer_is_encoding(void* encoding);

    // Buffer read methods
    int64_t ts_buffer_read_int8(void* buf, int64_t offset);
    int64_t ts_buffer_read_uint8(void* buf, int64_t offset);
    int64_t ts_buffer_read_int16le(void* buf, int64_t offset);
    int64_t ts_buffer_read_int16be(void* buf, int64_t offset);
    int64_t ts_buffer_read_uint16le(void* buf, int64_t offset);
    int64_t ts_buffer_read_uint16be(void* buf, int64_t offset);
    int64_t ts_buffer_read_int32le(void* buf, int64_t offset);
    int64_t ts_buffer_read_int32be(void* buf, int64_t offset);
    int64_t ts_buffer_read_uint32le(void* buf, int64_t offset);
    int64_t ts_buffer_read_uint32be(void* buf, int64_t offset);
    double ts_buffer_read_floatle(void* buf, int64_t offset);
    double ts_buffer_read_floatbe(void* buf, int64_t offset);
    double ts_buffer_read_doublele(void* buf, int64_t offset);
    double ts_buffer_read_doublebe(void* buf, int64_t offset);

    // Buffer write methods
    int64_t ts_buffer_write_int8(void* buf, int64_t value, int64_t offset);
    int64_t ts_buffer_write_uint8(void* buf, int64_t value, int64_t offset);
    int64_t ts_buffer_write_int16le(void* buf, int64_t value, int64_t offset);
    int64_t ts_buffer_write_int16be(void* buf, int64_t value, int64_t offset);
    int64_t ts_buffer_write_uint16le(void* buf, int64_t value, int64_t offset);
    int64_t ts_buffer_write_uint16be(void* buf, int64_t value, int64_t offset);
    int64_t ts_buffer_write_int32le(void* buf, int64_t value, int64_t offset);
    int64_t ts_buffer_write_int32be(void* buf, int64_t value, int64_t offset);
    int64_t ts_buffer_write_uint32le(void* buf, int64_t value, int64_t offset);
    int64_t ts_buffer_write_uint32be(void* buf, int64_t value, int64_t offset);
    int64_t ts_buffer_write_floatle(void* buf, double value, int64_t offset);
    int64_t ts_buffer_write_floatbe(void* buf, double value, int64_t offset);
    int64_t ts_buffer_write_doublele(void* buf, double value, int64_t offset);
    int64_t ts_buffer_write_doublebe(void* buf, double value, int64_t offset);

    // Buffer utility methods
    int64_t ts_buffer_compare(void* buf1, void* buf2);
    bool ts_buffer_equals(void* buf, void* other);
    int64_t ts_buffer_indexof(void* buf, int64_t value, int64_t byteOffset);
    int64_t ts_buffer_lastindexof(void* buf, int64_t value, int64_t byteOffset);
    bool ts_buffer_includes(void* buf, int64_t value, int64_t byteOffset);

    void* ts_typed_array_create_u8(int64_t length);
    void* ts_typed_array_create_u32(int64_t length);
    void* ts_typed_array_create_f64(int64_t length);
    void* ts_typed_array_from_array_u8(void* array);
    void* ts_typed_array_from_array_u32(void* array);
    void* ts_typed_array_from_array_f64(void* array);
    // Generic typed array functions with element size parameter
    void* ts_typed_array_create(int64_t length, int32_t elementSize);
    void* ts_typed_array_create_clamped(int64_t length);  // For Uint8ClampedArray
    void* ts_typed_array_from_array(void* array, int32_t elementSize);
    void* ts_typed_array_from_array_clamped(void* array);  // For Uint8ClampedArray
    int64_t ts_typed_array_length(void* typedArray);
    double ts_typed_array_get_generic(void* typedArray, int64_t index);
    void* ts_data_view_create(void* buffer);

    // DataView getters
    int64_t DataView_getInt8(void* context, void* dv, int64_t offset, bool littleEndian);
    int64_t DataView_getUint8(void* context, void* dv, int64_t offset, bool littleEndian);
    int64_t DataView_getInt16(void* context, void* dv, int64_t offset, bool littleEndian);
    int64_t DataView_getUint16(void* context, void* dv, int64_t offset, bool littleEndian);
    int64_t DataView_getInt32(void* context, void* dv, int64_t offset, bool littleEndian);
    int64_t DataView_getUint32(void* context, void* dv, int64_t offset, bool littleEndian);
    double DataView_getFloat32(void* context, void* dv, int64_t offset, bool littleEndian);
    double DataView_getFloat64(void* context, void* dv, int64_t offset, bool littleEndian);

    // DataView setters
    void DataView_setInt8(void* context, void* dv, int64_t offset, int64_t value, bool littleEndian);
    void DataView_setUint8(void* context, void* dv, int64_t offset, int64_t value, bool littleEndian);
    void DataView_setInt16(void* context, void* dv, int64_t offset, int64_t value, bool littleEndian);
    void DataView_setUint16(void* context, void* dv, int64_t offset, int64_t value, bool littleEndian);
    void DataView_setInt32(void* context, void* dv, int64_t offset, int64_t value, bool littleEndian);
    void DataView_setUint32(void* context, void* dv, int64_t offset, int64_t value, bool littleEndian);
    void DataView_setFloat32(void* context, void* dv, int64_t offset, double value, bool littleEndian);
    void DataView_setFloat64(void* context, void* dv, int64_t offset, double value, bool littleEndian);

    // DataView properties
    void* DataView_getBuffer(void* context, void* dv);
    int64_t DataView_getByteLength(void* context, void* dv);
    int64_t DataView_getByteOffset(void* context, void* dv);
}
