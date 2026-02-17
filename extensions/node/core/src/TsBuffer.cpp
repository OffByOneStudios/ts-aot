#include "TsBuffer.h"
#include "TsNanBox.h"
#include "GC.h"
#include "TsRuntime.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsBigInt.h"
#include <cstring>
#include <new>
#include <unicode/unistr.h>
#include <cstdio>

TsValue TsBuffer::GetPropertyVirtual(const char* key) {
    if (strcmp(key, "length") == 0 || strcmp(key, "byteLength") == 0) {
        TsValue v;
        v.type = ValueType::NUMBER_INT;
        v.i_val = (int64_t)GetLength();
        return v;
    }
    if (strcmp(key, "resizable") == 0) {
        TsValue v;
        v.type = ValueType::BOOLEAN;
        v.i_val = IsResizable() ? 1 : 0;
        return v;
    }
    if (strcmp(key, "maxByteLength") == 0) {
        TsValue v;
        v.type = ValueType::NUMBER_INT;
        v.i_val = IsResizable() ? (int64_t)GetMaxByteLength() : (int64_t)GetLength();
        return v;
    }
    return TsObject::GetPropertyVirtual(key);
}

TsValue TsBuffer::GetElementVirtual(int64_t index) {
    if (index < 0 || (size_t)index >= length) {
        TsValue v;
        v.type = ValueType::UNDEFINED;
        v.i_val = 0;
        return v;
    }
    TsValue v;
    v.type = ValueType::NUMBER_INT;
    v.i_val = data[index];
    return v;
}

TsBuffer* TsBuffer::Create(size_t length) {
    void* mem = ts_alloc(sizeof(TsBuffer));
    TsBuffer* buf = new(mem) TsBuffer(length);
    return buf;
}

TsBuffer* TsBuffer::CreateResizable(size_t length, size_t maxByteLength) {
    void* mem = ts_alloc(sizeof(TsBuffer));
    TsBuffer* buf = new(mem) TsBuffer(length, maxByteLength);
    return buf;
}

TsBuffer* TsBuffer::FromString(TsString* str, TsString* encoding) {
    if (!str) return nullptr;
    
    // Check encoding type
    if (encoding) {
        const char* enc = encoding->ToUtf8();
        if (strcmp(enc, "hex") == 0) {
            return FromHex(str);
        } else if (strcmp(enc, "base64") == 0 || strcmp(enc, "base64url") == 0) {
            return FromBase64(str);
        }
    }
    
    // Default: UTF-8
    const char* utf8 = str->ToUtf8();
    size_t len = std::strlen(utf8);
    TsBuffer* buf = Create(len);
    std::memcpy(buf->data, utf8, len);
    return buf;
}

TsBuffer* TsBuffer::FromBuffer(TsBuffer* source) {
    if (!source) return Create(0);
    size_t len = source->GetLength();
    TsBuffer* buf = Create(len);
    std::memcpy(buf->data, source->data, len);
    return buf;
}

TsBuffer* TsBuffer::FromArray(void* arrPtr) {
    if (!arrPtr) return Create(0);
    TsArray* arr = (TsArray*)arrPtr;
    size_t len = arr->Length();
    TsBuffer* buf = Create(len);
    for (size_t i = 0; i < len; i++) {
        // Use GetElementDouble to handle both specialized double arrays
        // and generic arrays correctly (TypeScript numbers are f64)
        double val = arr->GetElementDouble(i);
        buf->data[i] = (uint8_t)((int64_t)val & 0xFF);
    }
    return buf;
}

TsBuffer* TsBuffer::FromArrayBuffer(void* arrayBuffer, int64_t byteOffset, int64_t len) {
    if (!arrayBuffer) return Create(0);

    // Unbox if needed
    void* rawPtr = ts_nanbox_safe_unbox(arrayBuffer);

    // Check if it's a TsBuffer (our ArrayBuffer implementation)
    TsBuffer* srcBuf = dynamic_cast<TsBuffer*>((TsObject*)rawPtr);
    if (!srcBuf) {
        // Try checking magic number directly
        uint32_t magic = *(uint32_t*)rawPtr;
        if (magic == MAGIC) {
            srcBuf = (TsBuffer*)rawPtr;
        } else {
            return Create(0);
        }
    }

    size_t srcLen = srcBuf->GetLength();
    size_t offset = (byteOffset >= 0) ? (size_t)byteOffset : 0;

    // Clamp offset to source length
    if (offset > srcLen) offset = srcLen;

    // Calculate actual length
    size_t actualLen;
    if (len < 0) {
        // No length specified - use remainder
        actualLen = srcLen - offset;
    } else {
        actualLen = (size_t)len;
        // Clamp to available data
        if (offset + actualLen > srcLen) {
            actualLen = srcLen - offset;
        }
    }

    // Create new buffer and copy data
    // Note: In Node.js, Buffer.from(arrayBuffer) shares memory, but we copy for safety
    TsBuffer* buf = Create(actualLen);
    if (actualLen > 0) {
        std::memcpy(buf->data, srcBuf->data + offset, actualLen);
    }
    return buf;
}

TsBuffer::TsBuffer(size_t length) {
    this->magic = MAGIC;
    this->length = length;
    this->maxByteLength = 0;
    this->data = (uint8_t*)ts_alloc(length);
    std::memset(this->data, 0, length);
}

TsBuffer::TsBuffer(size_t length, size_t maxByteLength) {
    this->magic = MAGIC;
    this->length = length;
    this->maxByteLength = maxByteLength;
    // Allocate maxByteLength upfront so resize() doesn't need realloc
    size_t allocSize = maxByteLength > length ? maxByteLength : length;
    this->data = (uint8_t*)ts_alloc(allocSize);
    std::memset(this->data, 0, allocSize);
}

void TsBuffer::Resize(size_t newByteLength) {
    if (maxByteLength == 0) {
        ts_throw((TsValue*)ts_error_create(TsString::Create("TypeError: Cannot resize a non-resizable ArrayBuffer")));
        return;
    }
    if (newByteLength > maxByteLength) {
        ts_throw((TsValue*)ts_error_create(TsString::Create("RangeError: Invalid array buffer length")));
        return;
    }
    // Zero-fill newly exposed bytes
    if (newByteLength > length) {
        std::memset(data + length, 0, newByteLength - length);
    }
    length = newByteLength;
}

uint8_t TsBuffer::Get(size_t index) {
    if (index >= length) {
        ts_panic("Buffer index out of bounds");
    }
    return data[index];
}

void TsBuffer::Set(size_t index, uint8_t value) {
    if (index >= length) {
        ts_panic("Buffer index out of bounds");
    }
    data[index] = value;
}

TsString* TsBuffer::ToString(TsString* encoding) {
    // Check encoding type
    if (encoding) {
        const char* enc = encoding->ToUtf8();
        if (strcmp(enc, "hex") == 0) {
            return ToHex();
        } else if (strcmp(enc, "base64") == 0) {
            return ToBase64();
        } else if (strcmp(enc, "base64url") == 0) {
            return ToBase64Url();
        }
    }
    // Default: UTF-8
    char* utf8 = (char*)ts_alloc(length + 1);
    std::memcpy(utf8, data, length);
    utf8[length] = '\0';
    return TsString::Create(utf8);
}

// Hex encoding lookup table
static const char HEX_CHARS[] = "0123456789abcdef";

TsString* TsBuffer::ToHex() {
    char* hex = (char*)ts_alloc(length * 2 + 1);
    for (size_t i = 0; i < length; i++) {
        hex[i * 2] = HEX_CHARS[(data[i] >> 4) & 0xF];
        hex[i * 2 + 1] = HEX_CHARS[data[i] & 0xF];
    }
    hex[length * 2] = '\0';
    return TsString::Create(hex);
}

// Base64 encoding
static const char BASE64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char BASE64URL_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static TsString* toBase64Internal(uint8_t* data, size_t length, const char* chars, bool padding) {
    size_t outLen = ((length + 2) / 3) * 4;
    if (!padding) {
        // Remove padding from output length
        size_t mod = length % 3;
        if (mod == 1) outLen -= 2;
        else if (mod == 2) outLen -= 1;
    }
    char* out = (char*)ts_alloc(outLen + 1);
    
    size_t j = 0;
    for (size_t i = 0; i < length; i += 3) {
        uint32_t a = data[i];
        uint32_t b = (i + 1 < length) ? data[i + 1] : 0;
        uint32_t c = (i + 2 < length) ? data[i + 2] : 0;
        
        uint32_t triple = (a << 16) | (b << 8) | c;
        
        out[j++] = chars[(triple >> 18) & 0x3F];
        out[j++] = chars[(triple >> 12) & 0x3F];
        
        if (i + 1 < length || padding) {
            out[j++] = (i + 1 < length) ? chars[(triple >> 6) & 0x3F] : '=';
        }
        if (i + 2 < length || padding) {
            out[j++] = (i + 2 < length) ? chars[triple & 0x3F] : '=';
        }
    }
    out[j] = '\0';
    return TsString::Create(out);
}

TsString* TsBuffer::ToBase64() {
    return toBase64Internal(data, length, BASE64_CHARS, true);
}

TsString* TsBuffer::ToBase64Url() {
    return toBase64Internal(data, length, BASE64URL_CHARS, false);
}

// Decode hex string to buffer
TsBuffer* TsBuffer::FromHex(TsString* hexStr) {
    if (!hexStr) return Create(0);
    const char* hex = hexStr->ToUtf8();
    size_t hexLen = strlen(hex);
    if (hexLen % 2 != 0) return Create(0); // Invalid hex
    
    size_t bufLen = hexLen / 2;
    TsBuffer* buf = Create(bufLen);
    
    for (size_t i = 0; i < bufLen; i++) {
        char hi = hex[i * 2];
        char lo = hex[i * 2 + 1];
        
        uint8_t hiVal = 0, loVal = 0;
        if (hi >= '0' && hi <= '9') hiVal = hi - '0';
        else if (hi >= 'a' && hi <= 'f') hiVal = hi - 'a' + 10;
        else if (hi >= 'A' && hi <= 'F') hiVal = hi - 'A' + 10;
        
        if (lo >= '0' && lo <= '9') loVal = lo - '0';
        else if (lo >= 'a' && lo <= 'f') loVal = lo - 'a' + 10;
        else if (lo >= 'A' && lo <= 'F') loVal = lo - 'A' + 10;
        
        buf->data[i] = (hiVal << 4) | loVal;
    }
    return buf;
}

// Base64 decode lookup (returns -1 for invalid chars)
static int base64DecodeChar(char c, bool isUrl) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (isUrl) {
        if (c == '-') return 62;
        if (c == '_') return 63;
    } else {
        if (c == '+') return 62;
        if (c == '/') return 63;
    }
    return -1;
}

TsBuffer* TsBuffer::FromBase64(TsString* b64Str) {
    if (!b64Str) return Create(0);
    const char* b64 = b64Str->ToUtf8();
    size_t b64Len = strlen(b64);
    
    // Detect if it's base64url (has - or _)
    bool isUrl = false;
    for (size_t i = 0; i < b64Len; i++) {
        if (b64[i] == '-' || b64[i] == '_') {
            isUrl = true;
            break;
        }
    }
    
    // Remove padding and calculate length
    size_t padCount = 0;
    while (b64Len > 0 && b64[b64Len - 1] == '=') {
        padCount++;
        b64Len--;
    }
    
    size_t outLen = (b64Len * 3) / 4;
    TsBuffer* buf = Create(outLen);
    
    size_t j = 0;
    for (size_t i = 0; i < b64Len; i += 4) {
        int a = base64DecodeChar(b64[i], isUrl);
        int b = (i + 1 < b64Len) ? base64DecodeChar(b64[i + 1], isUrl) : 0;
        int c = (i + 2 < b64Len) ? base64DecodeChar(b64[i + 2], isUrl) : 0;
        int d = (i + 3 < b64Len) ? base64DecodeChar(b64[i + 3], isUrl) : 0;
        
        if (a < 0 || b < 0 || c < 0 || d < 0) continue; // Skip invalid
        
        uint32_t triple = (a << 18) | (b << 12) | (c << 6) | d;
        
        if (j < outLen) buf->data[j++] = (triple >> 16) & 0xFF;
        if (j < outLen && i + 2 < b64Len) buf->data[j++] = (triple >> 8) & 0xFF;
        if (j < outLen && i + 3 < b64Len) buf->data[j++] = triple & 0xFF;
    }
    
    return buf;
}

TsBuffer* TsBuffer::AllocUnsafe(size_t length) {
    // Same as Create but doesn't zero-fill
    void* mem = ts_alloc(sizeof(TsBuffer));
    TsBuffer* buf = (TsBuffer*)mem;
    buf->magic = MAGIC;
    buf->length = length;
    buf->data = (uint8_t*)ts_alloc(length);
    // Note: data is NOT zeroed - this is intentional for allocUnsafe
    return buf;
}

TsBuffer* TsBuffer::AllocUnsafeSlow(size_t length) {
    // In Node.js, this bypasses the internal buffer pool.
    // Since we use GC allocation directly, this is identical to AllocUnsafe.
    return AllocUnsafe(length);
}

TsBuffer* TsBuffer::Slice(int64_t start, int64_t end) {
    // Normalize negative indices
    int64_t len = (int64_t)length;
    if (end <= 0 && start >= 0) end = len;  // default: use buffer length
    if (start < 0) start = len + start;
    if (end < 0) end = len + end;
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if (start > len) start = len;
    if (end > len) end = len;
    if (end < start) end = start;
    
    size_t newLen = (size_t)(end - start);
    TsBuffer* result = Create(newLen);
    if (newLen > 0) {
        std::memcpy(result->data, data + start, newLen);
    }
    return result;
}

TsBuffer* TsBuffer::Subarray(int64_t start, int64_t end) {
    // In Node.js, subarray returns a view, but for simplicity we copy like slice
    return Slice(start, end);
}

void TsBuffer::Fill(uint8_t value, int64_t start, int64_t end) {
    int64_t len = (int64_t)length;
    if (end <= 0) end = len;  // default: use buffer length
    if (start < 0) start = len + start;
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) return;
    
    std::memset(data + start, value, (size_t)(end - start));
}

int64_t TsBuffer::Copy(TsBuffer* target, int64_t targetStart, int64_t sourceStart, int64_t sourceEnd) {
    if (!target) return 0;
    
    int64_t srcLen = (int64_t)length;
    int64_t tgtLen = (int64_t)target->length;
    
    if (sourceEnd <= 0) sourceEnd = srcLen;
    if (sourceStart < 0) sourceStart = 0;
    if (targetStart < 0) targetStart = 0;
    
    if (sourceStart >= srcLen) return 0;
    if (targetStart >= tgtLen) return 0;
    if (sourceEnd > srcLen) sourceEnd = srcLen;
    
    int64_t bytesToCopy = sourceEnd - sourceStart;
    if (bytesToCopy <= 0) return 0;
    
    // Limit by available space in target
    if (targetStart + bytesToCopy > tgtLen) {
        bytesToCopy = tgtLen - targetStart;
    }
    
    if (bytesToCopy <= 0) return 0;
    
    // Use memmove to handle overlapping buffers
    std::memmove(target->data + targetStart, data + sourceStart, (size_t)bytesToCopy);
    return bytesToCopy;
}

TsBuffer* TsBuffer::Concat(void* list, int64_t totalLength) {
    if (!list) return Create(0);

    TsArray* arr = (TsArray*)list;
    size_t count = (size_t)arr->Length();

    // Calculate total length if not provided (compiler passes 0 for omitted args)
    size_t calcLen = 0;
    if (totalLength <= 0) {
        for (size_t i = 0; i < count; i++) {
            uint64_t elem = (uint64_t)arr->Get(i);
            // NaN boxing: pointers have top 16 bits = 0 and value > 6
            if ((elem & 0xFFFF000000000000ULL) != 0 || elem <= 6) continue;
            TsBuffer* buf = (TsBuffer*)(uintptr_t)elem;
            if (buf->magic == MAGIC) {
                calcLen += buf->length;
            }
        }
    } else {
        calcLen = (size_t)totalLength;
    }

    TsBuffer* result = Create(calcLen);
    size_t offset = 0;

    for (size_t i = 0; i < count && offset < calcLen; i++) {
        uint64_t elem = (uint64_t)arr->Get(i);
        if ((elem & 0xFFFF000000000000ULL) != 0 || elem <= 6) continue;
        TsBuffer* buf = (TsBuffer*)(uintptr_t)elem;
        if (buf->magic == MAGIC) {
            size_t toCopy = buf->length;
            if (offset + toCopy > calcLen) {
                toCopy = calcLen - offset;
            }
            std::memcpy(result->data + offset, buf->data, toCopy);
            offset += toCopy;
        }
    }
    
    return result;
}

// ============================================================================
// Buffer Read Method Implementations
// ============================================================================

int8_t TsBuffer::ReadInt8(size_t offset) {
    if (offset >= length) return 0;
    return (int8_t)data[offset];
}

uint8_t TsBuffer::ReadUInt8(size_t offset) {
    if (offset >= length) return 0;
    return data[offset];
}

int16_t TsBuffer::ReadInt16LE(size_t offset) {
    if (offset + 2 > length) return 0;
    int16_t val;
    std::memcpy(&val, data + offset, 2);
    return val;  // Assume host is little endian
}

int16_t TsBuffer::ReadInt16BE(size_t offset) {
    if (offset + 2 > length) return 0;
    uint16_t val;
    std::memcpy(&val, data + offset, 2);
    val = (val >> 8) | (val << 8);  // Swap bytes
    return (int16_t)val;
}

uint16_t TsBuffer::ReadUInt16LE(size_t offset) {
    if (offset + 2 > length) return 0;
    uint16_t val;
    std::memcpy(&val, data + offset, 2);
    return val;
}

uint16_t TsBuffer::ReadUInt16BE(size_t offset) {
    if (offset + 2 > length) return 0;
    uint16_t val;
    std::memcpy(&val, data + offset, 2);
    return (val >> 8) | (val << 8);
}

int32_t TsBuffer::ReadInt32LE(size_t offset) {
    if (offset + 4 > length) return 0;
    int32_t val;
    std::memcpy(&val, data + offset, 4);
    return val;
}

int32_t TsBuffer::ReadInt32BE(size_t offset) {
    if (offset + 4 > length) return 0;
    uint32_t val;
    std::memcpy(&val, data + offset, 4);
    val = ((val >> 24) & 0xFF) | ((val >> 8) & 0xFF00) |
          ((val << 8) & 0xFF0000) | ((val << 24) & 0xFF000000);
    return (int32_t)val;
}

uint32_t TsBuffer::ReadUInt32LE(size_t offset) {
    if (offset + 4 > length) return 0;
    uint32_t val;
    std::memcpy(&val, data + offset, 4);
    return val;
}

uint32_t TsBuffer::ReadUInt32BE(size_t offset) {
    if (offset + 4 > length) return 0;
    uint32_t val;
    std::memcpy(&val, data + offset, 4);
    return ((val >> 24) & 0xFF) | ((val >> 8) & 0xFF00) |
           ((val << 8) & 0xFF0000) | ((val << 24) & 0xFF000000);
}

float TsBuffer::ReadFloatLE(size_t offset) {
    if (offset + 4 > length) return 0;
    float val;
    std::memcpy(&val, data + offset, 4);
    return val;
}

float TsBuffer::ReadFloatBE(size_t offset) {
    if (offset + 4 > length) return 0;
    uint32_t intVal;
    std::memcpy(&intVal, data + offset, 4);
    intVal = ((intVal >> 24) & 0xFF) | ((intVal >> 8) & 0xFF00) |
             ((intVal << 8) & 0xFF0000) | ((intVal << 24) & 0xFF000000);
    float val;
    std::memcpy(&val, &intVal, 4);
    return val;
}

double TsBuffer::ReadDoubleLE(size_t offset) {
    if (offset + 8 > length) return 0;
    double val;
    std::memcpy(&val, data + offset, 8);
    return val;
}

double TsBuffer::ReadDoubleBE(size_t offset) {
    if (offset + 8 > length) return 0;
    uint64_t intVal;
    std::memcpy(&intVal, data + offset, 8);
    intVal = ((intVal >> 56) & 0xFF) |
             ((intVal >> 40) & 0xFF00) |
             ((intVal >> 24) & 0xFF0000) |
             ((intVal >> 8) & 0xFF000000) |
             ((intVal << 8) & 0xFF00000000ULL) |
             ((intVal << 24) & 0xFF0000000000ULL) |
             ((intVal << 40) & 0xFF000000000000ULL) |
             ((intVal << 56) & 0xFF00000000000000ULL);
    double val;
    std::memcpy(&val, &intVal, 8);
    return val;
}

// ============================================================================
// Buffer Write Method Implementations
// ============================================================================

size_t TsBuffer::WriteInt8(int8_t value, size_t offset) {
    if (offset >= length) return offset;
    data[offset] = (uint8_t)value;
    return offset + 1;
}

size_t TsBuffer::WriteUInt8(uint8_t value, size_t offset) {
    if (offset >= length) return offset;
    data[offset] = value;
    return offset + 1;
}

size_t TsBuffer::WriteInt16LE(int16_t value, size_t offset) {
    if (offset + 2 > length) return offset;
    std::memcpy(data + offset, &value, 2);
    return offset + 2;
}

size_t TsBuffer::WriteInt16BE(int16_t value, size_t offset) {
    if (offset + 2 > length) return offset;
    uint16_t swapped = ((uint16_t)value >> 8) | ((uint16_t)value << 8);
    std::memcpy(data + offset, &swapped, 2);
    return offset + 2;
}

size_t TsBuffer::WriteUInt16LE(uint16_t value, size_t offset) {
    if (offset + 2 > length) return offset;
    std::memcpy(data + offset, &value, 2);
    return offset + 2;
}

size_t TsBuffer::WriteUInt16BE(uint16_t value, size_t offset) {
    if (offset + 2 > length) return offset;
    uint16_t swapped = (value >> 8) | (value << 8);
    std::memcpy(data + offset, &swapped, 2);
    return offset + 2;
}

size_t TsBuffer::WriteInt32LE(int32_t value, size_t offset) {
    if (offset + 4 > length) return offset;
    std::memcpy(data + offset, &value, 4);
    return offset + 4;
}

size_t TsBuffer::WriteInt32BE(int32_t value, size_t offset) {
    if (offset + 4 > length) return offset;
    uint32_t v = (uint32_t)value;
    uint32_t swapped = ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
                       ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
    std::memcpy(data + offset, &swapped, 4);
    return offset + 4;
}

size_t TsBuffer::WriteUInt32LE(uint32_t value, size_t offset) {
    if (offset + 4 > length) return offset;
    std::memcpy(data + offset, &value, 4);
    return offset + 4;
}

size_t TsBuffer::WriteUInt32BE(uint32_t value, size_t offset) {
    if (offset + 4 > length) return offset;
    uint32_t swapped = ((value >> 24) & 0xFF) | ((value >> 8) & 0xFF00) |
                       ((value << 8) & 0xFF0000) | ((value << 24) & 0xFF000000);
    std::memcpy(data + offset, &swapped, 4);
    return offset + 4;
}

size_t TsBuffer::WriteFloatLE(float value, size_t offset) {
    if (offset + 4 > length) return offset;
    std::memcpy(data + offset, &value, 4);
    return offset + 4;
}

size_t TsBuffer::WriteFloatBE(float value, size_t offset) {
    if (offset + 4 > length) return offset;
    uint32_t intVal;
    std::memcpy(&intVal, &value, 4);
    intVal = ((intVal >> 24) & 0xFF) | ((intVal >> 8) & 0xFF00) |
             ((intVal << 8) & 0xFF0000) | ((intVal << 24) & 0xFF000000);
    std::memcpy(data + offset, &intVal, 4);
    return offset + 4;
}

size_t TsBuffer::WriteDoubleLE(double value, size_t offset) {
    if (offset + 8 > length) return offset;
    std::memcpy(data + offset, &value, 8);
    return offset + 8;
}

size_t TsBuffer::WriteDoubleBE(double value, size_t offset) {
    if (offset + 8 > length) return offset;
    uint64_t intVal;
    std::memcpy(&intVal, &value, 8);
    intVal = ((intVal >> 56) & 0xFF) |
             ((intVal >> 40) & 0xFF00) |
             ((intVal >> 24) & 0xFF0000) |
             ((intVal >> 8) & 0xFF000000) |
             ((intVal << 8) & 0xFF00000000ULL) |
             ((intVal << 24) & 0xFF0000000000ULL) |
             ((intVal << 40) & 0xFF000000000000ULL) |
             ((intVal << 56) & 0xFF00000000000000ULL);
    std::memcpy(data + offset, &intVal, 8);
    return offset + 8;
}

// BigInt64 read methods
int64_t TsBuffer::ReadBigInt64LE(size_t offset) {
    if (offset + 8 > length) return 0;
    int64_t val;
    std::memcpy(&val, data + offset, 8);
    return val;  // Assume host is little endian
}

int64_t TsBuffer::ReadBigInt64BE(size_t offset) {
    if (offset + 8 > length) return 0;
    uint64_t val;
    std::memcpy(&val, data + offset, 8);
    val = ((val >> 56) & 0xFF) |
          ((val >> 40) & 0xFF00) |
          ((val >> 24) & 0xFF0000) |
          ((val >> 8) & 0xFF000000) |
          ((val << 8) & 0xFF00000000ULL) |
          ((val << 24) & 0xFF0000000000ULL) |
          ((val << 40) & 0xFF000000000000ULL) |
          ((val << 56) & 0xFF00000000000000ULL);
    return (int64_t)val;
}

uint64_t TsBuffer::ReadBigUInt64LE(size_t offset) {
    if (offset + 8 > length) return 0;
    uint64_t val;
    std::memcpy(&val, data + offset, 8);
    return val;  // Assume host is little endian
}

uint64_t TsBuffer::ReadBigUInt64BE(size_t offset) {
    if (offset + 8 > length) return 0;
    uint64_t val;
    std::memcpy(&val, data + offset, 8);
    return ((val >> 56) & 0xFF) |
           ((val >> 40) & 0xFF00) |
           ((val >> 24) & 0xFF0000) |
           ((val >> 8) & 0xFF000000) |
           ((val << 8) & 0xFF00000000ULL) |
           ((val << 24) & 0xFF0000000000ULL) |
           ((val << 40) & 0xFF000000000000ULL) |
           ((val << 56) & 0xFF00000000000000ULL);
}

// BigInt64 write methods
size_t TsBuffer::WriteBigInt64LE(int64_t value, size_t offset) {
    if (offset + 8 > length) return offset;
    std::memcpy(data + offset, &value, 8);
    return offset + 8;
}

size_t TsBuffer::WriteBigInt64BE(int64_t value, size_t offset) {
    if (offset + 8 > length) return offset;
    uint64_t v = (uint64_t)value;
    uint64_t swapped = ((v >> 56) & 0xFF) |
                       ((v >> 40) & 0xFF00) |
                       ((v >> 24) & 0xFF0000) |
                       ((v >> 8) & 0xFF000000) |
                       ((v << 8) & 0xFF00000000ULL) |
                       ((v << 24) & 0xFF0000000000ULL) |
                       ((v << 40) & 0xFF000000000000ULL) |
                       ((v << 56) & 0xFF00000000000000ULL);
    std::memcpy(data + offset, &swapped, 8);
    return offset + 8;
}

size_t TsBuffer::WriteBigUInt64LE(uint64_t value, size_t offset) {
    if (offset + 8 > length) return offset;
    std::memcpy(data + offset, &value, 8);
    return offset + 8;
}

size_t TsBuffer::WriteBigUInt64BE(uint64_t value, size_t offset) {
    if (offset + 8 > length) return offset;
    uint64_t swapped = ((value >> 56) & 0xFF) |
                       ((value >> 40) & 0xFF00) |
                       ((value >> 24) & 0xFF0000) |
                       ((value >> 8) & 0xFF000000) |
                       ((value << 8) & 0xFF00000000ULL) |
                       ((value << 24) & 0xFF0000000000ULL) |
                       ((value << 40) & 0xFF000000000000ULL) |
                       ((value << 56) & 0xFF00000000000000ULL);
    std::memcpy(data + offset, &swapped, 8);
    return offset + 8;
}

// ============================================================================
// Buffer Utility Method Implementations
// ============================================================================

int TsBuffer::Compare(TsBuffer* other) {
    if (!other) return 1;
    size_t minLen = length < other->length ? length : other->length;
    int cmp = std::memcmp(data, other->data, minLen);
    if (cmp != 0) return cmp;
    if (length < other->length) return -1;
    if (length > other->length) return 1;
    return 0;
}

int TsBuffer::Compare(TsBuffer* buf1, TsBuffer* buf2) {
    if (!buf1 && !buf2) return 0;
    if (!buf1) return -1;
    if (!buf2) return 1;
    return buf1->Compare(buf2);
}

bool TsBuffer::Equals(TsBuffer* other) {
    if (!other) return false;
    if (length != other->length) return false;
    return std::memcmp(data, other->data, length) == 0;
}

int64_t TsBuffer::IndexOf(uint8_t value, size_t byteOffset) {
    for (size_t i = byteOffset; i < length; i++) {
        if (data[i] == value) return (int64_t)i;
    }
    return -1;
}

int64_t TsBuffer::IndexOfBuffer(TsBuffer* value, size_t byteOffset) {
    if (!value || value->length == 0) return -1;
    if (value->length > length) return -1;
    for (size_t i = byteOffset; i <= length - value->length; i++) {
        if (std::memcmp(data + i, value->data, value->length) == 0) {
            return (int64_t)i;
        }
    }
    return -1;
}

int64_t TsBuffer::LastIndexOf(uint8_t value, size_t byteOffset) {
    if (length == 0) return -1;
    size_t start = byteOffset >= length ? length - 1 : byteOffset;
    for (size_t i = start + 1; i > 0; i--) {
        if (data[i - 1] == value) return (int64_t)(i - 1);
    }
    return -1;
}

bool TsBuffer::Includes(uint8_t value, size_t byteOffset) {
    return IndexOf(value, byteOffset) >= 0;
}

bool TsBuffer::IncludesBuffer(TsBuffer* value, size_t byteOffset) {
    return IndexOfBuffer(value, byteOffset) >= 0;
}

bool TsBuffer::IsEncoding(const char* encoding) {
    if (!encoding) return false;
    // Supported encodings
    return strcmp(encoding, "utf8") == 0 ||
           strcmp(encoding, "utf-8") == 0 ||
           strcmp(encoding, "hex") == 0 ||
           strcmp(encoding, "base64") == 0 ||
           strcmp(encoding, "base64url") == 0 ||
           strcmp(encoding, "ascii") == 0 ||
           strcmp(encoding, "latin1") == 0 ||
           strcmp(encoding, "binary") == 0 ||
           strcmp(encoding, "ucs2") == 0 ||
           strcmp(encoding, "ucs-2") == 0 ||
           strcmp(encoding, "utf16le") == 0 ||
           strcmp(encoding, "utf-16le") == 0;
}

void* TsBuffer::Entries() {
    TsArray* result = TsArray::Create();
    for (size_t i = 0; i < length; i++) {
        TsArray* pair = TsArray::Create();
        pair->Push((int64_t)nanbox_int32((int32_t)i));
        pair->Push((int64_t)nanbox_int32((int32_t)data[i]));
        result->Push(reinterpret_cast<int64_t>(pair));
    }
    return result;
}

void* TsBuffer::Keys() {
    TsArray* result = TsArray::Create();
    for (size_t i = 0; i < length; i++) {
        result->Push((int64_t)nanbox_int32((int32_t)i));
    }
    return result;
}

void* TsBuffer::Values() {
    TsArray* result = TsArray::Create();
    for (size_t i = 0; i < length; i++) {
        result->Push((int64_t)nanbox_int32((int32_t)data[i]));
    }
    return result;
}

void* TsBuffer::ToJSON() {
    // Returns { type: "Buffer", data: [byte values] }
    TsMap* obj = TsMap::Create();
    obj->Set(TsString::Create("type"), reinterpret_cast<int64_t>(TsString::Create("Buffer")));

    TsArray* dataArr = TsArray::Create();
    for (size_t i = 0; i < length; i++) {
        dataArr->Push((int64_t)nanbox_int32((int32_t)data[i]));
    }
    obj->Set(TsString::Create("data"), reinterpret_cast<int64_t>(dataArr));

    return obj;
}

extern "C" {
    void* ts_arraybuffer_create(int64_t length) {
        return TsBuffer::Create((size_t)length);
    }

    void* ts_arraybuffer_create_with_options(int64_t length, void* options) {
        if (!options) return TsBuffer::Create((size_t)length);
        // Extract maxByteLength from options object
        // options is a TsMap or flat object with maxByteLength property
        TsValue* maxByteLengthVal = ts_object_get_dynamic((TsValue*)options, (TsValue*)TsString::Create("maxByteLength"));
        if (maxByteLengthVal && !ts_value_is_undefined(maxByteLengthVal)) {
            int64_t maxByteLength = ts_value_get_int(maxByteLengthVal);
            if (maxByteLength < (int64_t)length) {
                ts_throw((TsValue*)ts_error_create(TsString::Create("RangeError: maxByteLength must be >= byteLength")));
                return nullptr;
            }
            return TsBuffer::CreateResizable((size_t)length, (size_t)maxByteLength);
        }
        return TsBuffer::Create((size_t)length);
    }

    void ts_arraybuffer_resize(void* buf, double newByteLength) {
        if (!buf || (uint64_t)(uintptr_t)buf < 0x10000) return;
        ((TsBuffer*)buf)->Resize((size_t)(int64_t)newByteLength);
    }

    bool ts_arraybuffer_get_resizable(void* buf) {
        if (!buf || (uint64_t)(uintptr_t)buf < 0x10000) return false;
        return ((TsBuffer*)buf)->IsResizable();
    }

    int64_t ts_arraybuffer_get_max_byte_length(void* buf) {
        if (!buf || (uint64_t)(uintptr_t)buf < 0x10000) return 0;
        TsBuffer* buffer = (TsBuffer*)buf;
        // If not resizable, maxByteLength === byteLength per spec
        if (!buffer->IsResizable()) return (int64_t)buffer->GetLength();
        return (int64_t)buffer->GetMaxByteLength();
    }

    void* ts_buffer_alloc(int64_t length) {
        return TsBuffer::Create((size_t)length);
    }

    void* ts_buffer_alloc_unsafe(int64_t length) {
        return TsBuffer::AllocUnsafe((size_t)length);
    }

    void* ts_buffer_alloc_unsafe_slow(int64_t length) {
        return TsBuffer::AllocUnsafeSlow((size_t)length);
    }

    void* ts_buffer_from(void* data) {
        // Handle string or array
        if (!data) return TsBuffer::Create(0);
        
        // Try to detect type - check for TsArray magic
        TsArray* arr = (TsArray*)data;
        // If it's an array, create buffer from array
        // For now, assume it's a string
        return TsBuffer::FromString((TsString*)data, nullptr);
    }

    void* ts_buffer_from_string(void* str, void* encoding) {
        // Check if the argument is actually an array (Buffer.from([...]))
        if (str) {
            uint32_t magic = *(uint32_t*)str;
            if (magic == 0x41525259) { // TsArray MAGIC "ARRY"
                return TsBuffer::FromArray(str);
            }
        }
        return TsBuffer::FromString((TsString*)str, (TsString*)encoding);
    }

    void* ts_buffer_from_buffer(void* buf) {
        if (!buf) return TsBuffer::Create(0);
        // Unbox if needed
        void* rawPtr = ts_nanbox_safe_unbox(buf);
        TsBuffer* srcBuf = dynamic_cast<TsBuffer*>((TsObject*)rawPtr);
        if (!srcBuf) return TsBuffer::Create(0);
        return TsBuffer::FromBuffer(srcBuf);
    }

    void* ts_buffer_from_array(void* arr) {
        if (!arr) return TsBuffer::Create(0);
        return TsBuffer::FromArray(arr);
    }

    void* ts_buffer_from_arraybuffer(void* arrayBuffer, int64_t byteOffset, int64_t length) {
        return TsBuffer::FromArrayBuffer(arrayBuffer, byteOffset, length);
    }

    void* ts_buffer_concat(void* list, int64_t totalLength) {
        return TsBuffer::Concat(list, totalLength);
    }

    int64_t ts_buffer_length(void* buf) {
        if (!buf || (uint64_t)(uintptr_t)buf < 0x10000) return 0;
        return (int64_t)((TsBuffer*)buf)->GetLength();
    }

    int64_t ts_buffer_byte_length(void* buf) {
        if (!buf || (uint64_t)(uintptr_t)buf < 0x10000) return 0;
        return (int64_t)((TsBuffer*)buf)->GetByteLength();
    }

    int64_t ts_buffer_byte_offset(void* buf) {
        if (!buf || (uint64_t)(uintptr_t)buf < 0x10000) return 0;
        return (int64_t)((TsBuffer*)buf)->GetByteOffset();
    }

    void* ts_buffer_get_array_buffer(void* buf) {
        if (!buf) return nullptr;
        return ((TsBuffer*)buf)->GetArrayBuffer();
    }

    uint8_t ts_buffer_get(void* buf, int64_t index) {
        if (!buf) return 0;
        return ((TsBuffer*)buf)->Get((size_t)index);
    }

    void ts_buffer_set(void* buf, int64_t index, uint8_t value) {
        if (!buf) return;
        ((TsBuffer*)buf)->Set((size_t)index, value);
    }

    void* ts_buffer_to_string(void* buf, void* encoding) {
        if (!buf) return nullptr;
        
        // Try to unbox if it's a TsValue*
        void* raw = ts_value_get_object((TsValue*)buf);
        TsBuffer* buffer = nullptr;
        if (raw) {
            buffer = (TsBuffer*)raw;
        } else {
            buffer = (TsBuffer*)buf;
        }
        
        // Validate it's actually a buffer
        if (buffer->magic != TsBuffer::MAGIC) {
            return TsString::Create("");
        }
        
        return buffer->ToString((TsString*)encoding);
    }

    static TsBuffer* getBuffer(void* buf) {
        if (!buf) return nullptr;
        void* raw = ts_value_get_object((TsValue*)buf);
        if (raw) buf = raw;
        TsBuffer* buffer = (TsBuffer*)buf;
        if (buffer->magic != TsBuffer::MAGIC) return nullptr;
        return buffer;
    }

    void* ts_buffer_slice(void* buf, int64_t start, int64_t end) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return TsBuffer::Create(0);
        return buffer->Slice(start, end);
    }

    void* ts_buffer_subarray(void* buf, int64_t start, int64_t end) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return TsBuffer::Create(0);
        return buffer->Subarray(start, end);
    }

    void* ts_buffer_fill(void* buf, int64_t value, int64_t start, int64_t end) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return nullptr;
        buffer->Fill((uint8_t)value, start, end);
        return buffer;  // Return this for chaining
    }

    int64_t ts_buffer_copy(void* source, void* target, int64_t targetStart, int64_t sourceStart, int64_t sourceEnd) {
        TsBuffer* src = getBuffer(source);
        TsBuffer* tgt = getBuffer(target);
        if (!src || !tgt) return 0;
        return src->Copy(tgt, targetStart, sourceStart, sourceEnd);
    }

    bool ts_buffer_is_buffer(void* obj) {
        if (!obj) return false;

        // Unbox NaN-boxed value - returns nullptr for non-pointer values (numbers, bool, etc.)
        uint64_t nb = (uint64_t)(uintptr_t)obj;
        if (nanbox_is_number(nb) || nanbox_is_special(nb)) return false;

        void* raw = ts_value_get_object((TsValue*)obj);
        if (raw) obj = raw;

        TsBuffer* buf = (TsBuffer*)obj;
        return buf->magic == TsBuffer::MAGIC;
    }

    bool ts_buffer_is_encoding(void* encoding) {
        if (!encoding) return false;
        TsString* enc = (TsString*)encoding;
        const char* encStr = enc->ToUtf8();
        return TsBuffer::IsEncoding(encStr);
    }

    int64_t ts_buffer_read_int8(void* buf, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->ReadInt8((size_t)offset);
    }

    int64_t ts_buffer_read_uint8(void* buf, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->ReadUInt8((size_t)offset);
    }

    int64_t ts_buffer_read_int16le(void* buf, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->ReadInt16LE((size_t)offset);
    }

    int64_t ts_buffer_read_int16be(void* buf, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->ReadInt16BE((size_t)offset);
    }

    int64_t ts_buffer_read_uint16le(void* buf, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->ReadUInt16LE((size_t)offset);
    }

    int64_t ts_buffer_read_uint16be(void* buf, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->ReadUInt16BE((size_t)offset);
    }

    int64_t ts_buffer_read_int32le(void* buf, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->ReadInt32LE((size_t)offset);
    }

    int64_t ts_buffer_read_int32be(void* buf, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->ReadInt32BE((size_t)offset);
    }

    int64_t ts_buffer_read_uint32le(void* buf, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->ReadUInt32LE((size_t)offset);
    }

    int64_t ts_buffer_read_uint32be(void* buf, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->ReadUInt32BE((size_t)offset);
    }

    double ts_buffer_read_floatle(void* buf, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (double)buffer->ReadFloatLE((size_t)offset);
    }

    double ts_buffer_read_floatbe(void* buf, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (double)buffer->ReadFloatBE((size_t)offset);
    }

    double ts_buffer_read_doublele(void* buf, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return buffer->ReadDoubleLE((size_t)offset);
    }

    double ts_buffer_read_doublebe(void* buf, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return buffer->ReadDoubleBE((size_t)offset);
    }

    void* ts_buffer_read_bigint64le(void* buf, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return ts_bigint_create_int(0);
        return ts_bigint_create_int(buffer->ReadBigInt64LE((size_t)offset));
    }

    void* ts_buffer_read_bigint64be(void* buf, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return ts_bigint_create_int(0);
        return ts_bigint_create_int(buffer->ReadBigInt64BE((size_t)offset));
    }

    void* ts_buffer_read_biguint64le(void* buf, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return ts_bigint_create_int(0);
        // Cast to int64_t for BigInt creation - the BigInt library handles this correctly
        return ts_bigint_create_int((int64_t)buffer->ReadBigUInt64LE((size_t)offset));
    }

    void* ts_buffer_read_biguint64be(void* buf, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return ts_bigint_create_int(0);
        // Cast to int64_t for BigInt creation - the BigInt library handles this correctly
        return ts_bigint_create_int((int64_t)buffer->ReadBigUInt64BE((size_t)offset));
    }

    // ============================================================================
    // Buffer Write Methods
    // ============================================================================

    int64_t ts_buffer_write_int8(void* buf, int64_t value, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->WriteInt8((int8_t)value, (size_t)offset);
    }

    int64_t ts_buffer_write_uint8(void* buf, int64_t value, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->WriteUInt8((uint8_t)value, (size_t)offset);
    }

    int64_t ts_buffer_write_int16le(void* buf, int64_t value, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->WriteInt16LE((int16_t)value, (size_t)offset);
    }

    int64_t ts_buffer_write_int16be(void* buf, int64_t value, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->WriteInt16BE((int16_t)value, (size_t)offset);
    }

    int64_t ts_buffer_write_uint16le(void* buf, int64_t value, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->WriteUInt16LE((uint16_t)value, (size_t)offset);
    }

    int64_t ts_buffer_write_uint16be(void* buf, int64_t value, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->WriteUInt16BE((uint16_t)value, (size_t)offset);
    }

    int64_t ts_buffer_write_int32le(void* buf, int64_t value, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->WriteInt32LE((int32_t)value, (size_t)offset);
    }

    int64_t ts_buffer_write_int32be(void* buf, int64_t value, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->WriteInt32BE((int32_t)value, (size_t)offset);
    }

    int64_t ts_buffer_write_uint32le(void* buf, int64_t value, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->WriteUInt32LE((uint32_t)value, (size_t)offset);
    }

    int64_t ts_buffer_write_uint32be(void* buf, int64_t value, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->WriteUInt32BE((uint32_t)value, (size_t)offset);
    }

    int64_t ts_buffer_write_floatle(void* buf, double value, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->WriteFloatLE((float)value, (size_t)offset);
    }

    int64_t ts_buffer_write_floatbe(void* buf, double value, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->WriteFloatBE((float)value, (size_t)offset);
    }

    int64_t ts_buffer_write_doublele(void* buf, double value, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->WriteDoubleLE(value, (size_t)offset);
    }

    int64_t ts_buffer_write_doublebe(void* buf, double value, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        return (int64_t)buffer->WriteDoubleBE(value, (size_t)offset);
    }

    int64_t ts_buffer_write_bigint64le(void* buf, void* value, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        // Extract int64_t from BigInt
        TsBigInt* bi = (TsBigInt*)value;
        if (!bi) return 0;
        int64_t val = mp_get_i64(&bi->value);
        return (int64_t)buffer->WriteBigInt64LE(val, (size_t)offset);
    }

    int64_t ts_buffer_write_bigint64be(void* buf, void* value, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        TsBigInt* bi = (TsBigInt*)value;
        if (!bi) return 0;
        int64_t val = mp_get_i64(&bi->value);
        return (int64_t)buffer->WriteBigInt64BE(val, (size_t)offset);
    }

    int64_t ts_buffer_write_biguint64le(void* buf, void* value, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        TsBigInt* bi = (TsBigInt*)value;
        if (!bi) return 0;
        // Get as unsigned - use mp_get_u64 if value is positive
        uint64_t val = (uint64_t)mp_get_i64(&bi->value);
        return (int64_t)buffer->WriteBigUInt64LE(val, (size_t)offset);
    }

    int64_t ts_buffer_write_biguint64be(void* buf, void* value, int64_t offset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return 0;
        TsBigInt* bi = (TsBigInt*)value;
        if (!bi) return 0;
        uint64_t val = (uint64_t)mp_get_i64(&bi->value);
        return (int64_t)buffer->WriteBigUInt64BE(val, (size_t)offset);
    }

    // ============================================================================
    // Buffer Variable-Length Read Methods
    // ============================================================================

    int64_t ts_buffer_read_intle(void* buf, int64_t offset, int64_t byteLength) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer || byteLength < 1 || byteLength > 6) return 0;
        size_t off = (size_t)offset;
        size_t len = buffer->GetLength();
        uint8_t* data = buffer->GetData();
        if (off + byteLength > len) return 0;

        int64_t value = 0;
        for (int64_t i = 0; i < byteLength; i++) {
            value |= ((int64_t)data[off + i]) << (i * 8);
        }
        // Sign-extend if negative
        int64_t signBit = 1LL << (byteLength * 8 - 1);
        if (value & signBit) {
            value |= ~((1LL << (byteLength * 8)) - 1);
        }
        return value;
    }

    int64_t ts_buffer_read_intbe(void* buf, int64_t offset, int64_t byteLength) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer || byteLength < 1 || byteLength > 6) return 0;
        size_t off = (size_t)offset;
        size_t len = buffer->GetLength();
        uint8_t* data = buffer->GetData();
        if (off + byteLength > len) return 0;

        int64_t value = 0;
        for (int64_t i = 0; i < byteLength; i++) {
            value = (value << 8) | data[off + i];
        }
        // Sign-extend if negative
        int64_t signBit = 1LL << (byteLength * 8 - 1);
        if (value & signBit) {
            value |= ~((1LL << (byteLength * 8)) - 1);
        }
        return value;
    }

    int64_t ts_buffer_read_uintle(void* buf, int64_t offset, int64_t byteLength) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer || byteLength < 1 || byteLength > 6) return 0;
        size_t off = (size_t)offset;
        size_t len = buffer->GetLength();
        uint8_t* data = buffer->GetData();
        if (off + byteLength > len) return 0;

        int64_t value = 0;
        for (int64_t i = 0; i < byteLength; i++) {
            value |= ((int64_t)data[off + i]) << (i * 8);
        }
        return value;
    }

    int64_t ts_buffer_read_uintbe(void* buf, int64_t offset, int64_t byteLength) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer || byteLength < 1 || byteLength > 6) return 0;
        size_t off = (size_t)offset;
        size_t len = buffer->GetLength();
        uint8_t* data = buffer->GetData();
        if (off + byteLength > len) return 0;

        int64_t value = 0;
        for (int64_t i = 0; i < byteLength; i++) {
            value = (value << 8) | data[off + i];
        }
        return value;
    }

    // ============================================================================
    // Buffer Variable-Length Write Methods
    // ============================================================================

    int64_t ts_buffer_write_intle(void* buf, int64_t value, int64_t offset, int64_t byteLength) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer || byteLength < 1 || byteLength > 6) return 0;
        size_t off = (size_t)offset;
        size_t len = buffer->GetLength();
        uint8_t* data = buffer->GetData();
        if (off + byteLength > len) return 0;

        for (int64_t i = 0; i < byteLength; i++) {
            data[off + i] = (uint8_t)(value >> (i * 8));
        }
        return offset + byteLength;
    }

    int64_t ts_buffer_write_intbe(void* buf, int64_t value, int64_t offset, int64_t byteLength) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer || byteLength < 1 || byteLength > 6) return 0;
        size_t off = (size_t)offset;
        size_t len = buffer->GetLength();
        uint8_t* data = buffer->GetData();
        if (off + byteLength > len) return 0;

        for (int64_t i = byteLength - 1; i >= 0; i--) {
            data[off + (byteLength - 1 - i)] = (uint8_t)(value >> (i * 8));
        }
        return offset + byteLength;
    }

    int64_t ts_buffer_write_uintle(void* buf, int64_t value, int64_t offset, int64_t byteLength) {
        // Same implementation as write_intle - unsigned values are stored the same way
        return ts_buffer_write_intle(buf, value, offset, byteLength);
    }

    int64_t ts_buffer_write_uintbe(void* buf, int64_t value, int64_t offset, int64_t byteLength) {
        // Same implementation as write_intbe - unsigned values are stored the same way
        return ts_buffer_write_intbe(buf, value, offset, byteLength);
    }

    // ============================================================================
    // Buffer String Write Method
    // ============================================================================

    int64_t ts_buffer_write_string(void* buf, void* str, int64_t offset, int64_t length, void* encoding) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer || !str) return 0;
        // Unbox NaN-boxed string pointer
        void* rawStr = ts_value_get_object((TsValue*)str);
        if (!rawStr) rawStr = str;
        TsString* tsStr = (TsString*)rawStr;
        const char* utf8 = tsStr->ToUtf8();
        size_t strLen = std::strlen(utf8);
        size_t bufLen = buffer->GetLength();

        if (offset < 0) offset = 0;
        if ((size_t)offset >= bufLen) return 0;

        size_t available = bufLen - (size_t)offset;
        size_t writeLen = strLen;
        if (length > 0 && (size_t)length < writeLen) writeLen = (size_t)length;
        if (writeLen > available) writeLen = available;

        std::memcpy(buffer->GetData() + offset, utf8, writeLen);
        return (int64_t)writeLen;
    }

    // ============================================================================
    // Buffer Swap Methods
    // ============================================================================

    void* ts_buffer_swap16(void* buf) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return buf;
        size_t len = buffer->GetLength();
        uint8_t* data = buffer->GetData();
        if (len % 2 != 0) return buf; // Must be divisible by 2

        for (size_t i = 0; i < len; i += 2) {
            uint8_t temp = data[i];
            data[i] = data[i + 1];
            data[i + 1] = temp;
        }
        return buf;
    }

    void* ts_buffer_swap32(void* buf) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return buf;
        size_t len = buffer->GetLength();
        uint8_t* data = buffer->GetData();
        if (len % 4 != 0) return buf; // Must be divisible by 4

        for (size_t i = 0; i < len; i += 4) {
            uint8_t t0 = data[i];
            uint8_t t1 = data[i + 1];
            data[i] = data[i + 3];
            data[i + 1] = data[i + 2];
            data[i + 2] = t1;
            data[i + 3] = t0;
        }
        return buf;
    }

    void* ts_buffer_swap64(void* buf) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return buf;
        size_t len = buffer->GetLength();
        uint8_t* data = buffer->GetData();
        if (len % 8 != 0) return buf; // Must be divisible by 8

        for (size_t i = 0; i < len; i += 8) {
            uint8_t t0 = data[i];
            uint8_t t1 = data[i + 1];
            uint8_t t2 = data[i + 2];
            uint8_t t3 = data[i + 3];
            data[i] = data[i + 7];
            data[i + 1] = data[i + 6];
            data[i + 2] = data[i + 5];
            data[i + 3] = data[i + 4];
            data[i + 4] = t3;
            data[i + 5] = t2;
            data[i + 6] = t1;
            data[i + 7] = t0;
        }
        return buf;
    }

    // ============================================================================
    // Buffer Utility Methods
    // ============================================================================

    int64_t ts_buffer_compare(void* buf1, void* buf2) {
        TsBuffer* b1 = getBuffer(buf1);
        TsBuffer* b2 = getBuffer(buf2);
        return (int64_t)TsBuffer::Compare(b1, b2);
    }

    bool ts_buffer_equals(void* buf, void* other) {
        TsBuffer* b1 = getBuffer(buf);
        TsBuffer* b2 = getBuffer(other);
        if (!b1 || !b2) return false;
        return b1->Equals(b2);
    }

    int64_t ts_buffer_indexof(void* buf, int64_t value, int64_t byteOffset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return -1;
        return buffer->IndexOf((uint8_t)value, (size_t)byteOffset);
    }

    int64_t ts_buffer_lastindexof(void* buf, int64_t value, int64_t byteOffset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return -1;
        return buffer->LastIndexOf((uint8_t)value, byteOffset < 0 ? SIZE_MAX : (size_t)byteOffset);
    }

    bool ts_buffer_includes(void* buf, int64_t value, int64_t byteOffset) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return false;
        return buffer->Includes((uint8_t)value, (size_t)byteOffset);
    }

    void* ts_buffer_entries(void* buf) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return TsArray::Create();
        return buffer->Entries();
    }

    void* ts_buffer_keys(void* buf) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return TsArray::Create();
        return buffer->Keys();
    }

    void* ts_buffer_values(void* buf) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) return TsArray::Create();
        return buffer->Values();
    }

    void* ts_buffer_to_json(void* buf) {
        TsBuffer* buffer = getBuffer(buf);
        if (!buffer) {
            // Return empty object
            return TsMap::Create();
        }
        return buffer->ToJSON();
    }

    void* Buffer_toString(void* context, void* buf) {
        if (!buf) return nullptr;
        return ((TsBuffer*)buf)->ToString(nullptr);
    }

    void* Buffer_static_from(void* str, void* encoding) {
        return TsBuffer::FromString((TsString*)str, (TsString*)encoding);
    }

    void* Buffer_static_alloc(int64_t length) {
        return TsBuffer::Create((size_t)length);
    }

    void* Buffer_get_length(void* buf) {
        if (!buf) return ts_value_make_double(0);
        // Unbox if needed - buf may be a TsValue* or raw TsBuffer*
        void* rawBuf = ts_value_get_object((TsValue*)buf);
        fprintf(stderr, "DEBUG Buffer_get_length: buf=%p, rawBuf=%p\n", buf, rawBuf);
        if (!rawBuf) rawBuf = buf;
        TsBuffer* tbuf = (TsBuffer*)rawBuf;
        fprintf(stderr, "DEBUG Buffer_get_length: tbuf=%p, length=%zu\n", tbuf, tbuf->GetLength());
        return ts_value_make_double(tbuf->GetLength());
    }

    void* ts_typed_array_create_u8(int64_t length) {
        return TsTypedArray::Create((size_t)length, 1, false, TypedArrayType::Uint8);
    }

    void* ts_typed_array_create_u32(int64_t length) {
        return TsTypedArray::Create((size_t)length, 4, false, TypedArrayType::Uint32);
    }

    void* ts_typed_array_create_f64(int64_t length) {
        return TsTypedArray::Create((size_t)length, 8, false, TypedArrayType::Float64);
    }

    void* ts_typed_array_from_array_u8(void* array) {
        if (!array) return nullptr;
        TsArray* arr = (TsArray*)array;
        size_t len = (size_t)arr->Length();
        TsTypedArray* ta = TsTypedArray::Create(len, 1, false, TypedArrayType::Uint8);
        uint8_t* data = ta->GetData();
        bool specialized = arr->IsSpecialized();
        bool isDouble = arr->IsDouble();
        for (size_t i = 0; i < len; i++) {
            if (specialized) {
                if (isDouble) {
                    int64_t bits = arr->Get(i);
                    data[i] = (uint8_t)(*(double*)&bits);
                } else {
                    data[i] = (uint8_t)arr->Get(i);
                }
            } else {
                data[i] = (uint8_t)ts_value_get_int((TsValue*)arr->Get(i));
            }
        }
        return ta;
    }

    void* ts_typed_array_from_array_u32(void* array) {
        if (!array) return nullptr;
        TsArray* arr = (TsArray*)array;
        size_t len = (size_t)arr->Length();
        TsTypedArray* ta = TsTypedArray::Create(len, 4, false, TypedArrayType::Uint32);
        uint32_t* data = (uint32_t*)ta->GetData();
        bool specialized = arr->IsSpecialized();
        bool isDouble = arr->IsDouble();
        for (size_t i = 0; i < len; i++) {
            if (specialized) {
                if (isDouble) {
                    int64_t bits = arr->Get(i);
                    data[i] = (uint32_t)(*(double*)&bits);
                } else {
                    data[i] = (uint32_t)arr->Get(i);
                }
            } else {
                data[i] = (uint32_t)ts_value_get_int((TsValue*)arr->Get(i));
            }
        }
        return ta;
    }

    void* ts_typed_array_from_array_f64(void* array) {
        if (!array) return nullptr;
        TsArray* arr = (TsArray*)array;
        size_t len = (size_t)arr->Length();
        TsTypedArray* ta = TsTypedArray::Create(len, 8, false, TypedArrayType::Float64);
        double* data = (double*)ta->GetData();
        bool specialized = arr->IsSpecialized();
        bool isDouble = arr->IsDouble();
        for (size_t i = 0; i < len; i++) {
            if (specialized) {
                int64_t val = arr->Get(i);
                if (isDouble) {
                    data[i] = *(double*)&val;
                } else {
                    data[i] = (double)val;
                }
            } else {
                data[i] = ts_value_get_double((TsValue*)arr->Get(i));
            }
        }
        return ta;
    }

    // Generic typed array create with element size parameter
    void* ts_typed_array_create(int64_t length, int32_t elementSize) {
        // Infer type from element size (defaults to signed/int for ambiguous sizes)
        TypedArrayType type = TypedArrayType::Uint8;
        switch (elementSize) {
            case 1: type = TypedArrayType::Int8; break;
            case 2: type = TypedArrayType::Int16; break;
            case 4: type = TypedArrayType::Int32; break;
            case 8: type = TypedArrayType::Float64; break;
        }
        return TsTypedArray::Create((size_t)length, (size_t)elementSize, false, type);
    }

    // Uint8ClampedArray create
    void* ts_typed_array_create_clamped(int64_t length) {
        return TsTypedArray::Create((size_t)length, 1, true, TypedArrayType::Uint8Clamped);
    }

    // Create typed array with explicit type (for BigInt64Array, BigUint64Array, etc.)
    void* ts_typed_array_create_typed(int64_t length, int32_t typeVal) {
        TypedArrayType type = static_cast<TypedArrayType>(typeVal);
        size_t elementSize = 1;
        switch (type) {
            case TypedArrayType::Int8:
            case TypedArrayType::Uint8:
            case TypedArrayType::Uint8Clamped:
                elementSize = 1;
                break;
            case TypedArrayType::Int16:
            case TypedArrayType::Uint16:
                elementSize = 2;
                break;
            case TypedArrayType::Int32:
            case TypedArrayType::Uint32:
            case TypedArrayType::Float32:
                elementSize = 4;
                break;
            case TypedArrayType::Float64:
            case TypedArrayType::BigInt64:
            case TypedArrayType::BigUint64:
                elementSize = 8;
                break;
        }
        bool clamped = (type == TypedArrayType::Uint8Clamped);
        return TsTypedArray::Create((size_t)length, elementSize, clamped, type);
    }

    // Create typed array from array with explicit type
    void* ts_typed_array_from_array_typed(void* array, int32_t typeVal) {
        if (!array) return nullptr;
        TsArray* arr = (TsArray*)array;
        size_t len = (size_t)arr->Length();
        TypedArrayType type = static_cast<TypedArrayType>(typeVal);
        size_t elementSize = 1;
        switch (type) {
            case TypedArrayType::Int8:
            case TypedArrayType::Uint8:
            case TypedArrayType::Uint8Clamped:
                elementSize = 1;
                break;
            case TypedArrayType::Int16:
            case TypedArrayType::Uint16:
                elementSize = 2;
                break;
            case TypedArrayType::Int32:
            case TypedArrayType::Uint32:
            case TypedArrayType::Float32:
                elementSize = 4;
                break;
            case TypedArrayType::Float64:
            case TypedArrayType::BigInt64:
            case TypedArrayType::BigUint64:
                elementSize = 8;
                break;
        }
        bool clamped = (type == TypedArrayType::Uint8Clamped);
        TsTypedArray* result = TsTypedArray::Create(len, elementSize, clamped, type);
        // Copy data from array
        for (size_t i = 0; i < len; i++) {
            int64_t val = arr->Get(i);
            if (type == TypedArrayType::BigInt64 || type == TypedArrayType::BigUint64) {
                // For BigInt arrays, store as 64-bit integers
                *(int64_t*)(result->GetData() + i * elementSize) = val;
            } else if (type == TypedArrayType::Float64) {
                *(double*)(result->GetData() + i * elementSize) = (double)val;
            } else if (type == TypedArrayType::Float32) {
                *(float*)(result->GetData() + i * elementSize) = (float)val;
            } else {
                // For integer types, just set the value
                result->Set((int32_t)i, val);
            }
        }
        return result;
    }

    // Generic typed array from array with element size parameter
    void* ts_typed_array_from_array(void* array, int32_t elementSize) {
        if (!array) return nullptr;
        TsArray* arr = (TsArray*)array;
        size_t len = (size_t)arr->Length();
        // Infer type from element size (defaults to signed/int for ambiguous sizes)
        TypedArrayType type = TypedArrayType::Uint8;
        switch (elementSize) {
            case 1: type = TypedArrayType::Int8; break;
            case 2: type = TypedArrayType::Int16; break;
            case 4: type = TypedArrayType::Int32; break;
            case 8: type = TypedArrayType::Float64; break;
        }
        TsTypedArray* ta = TsTypedArray::Create(len, (size_t)elementSize, false, type);
        uint8_t* data = ta->GetData();
        bool specialized = arr->IsSpecialized();
        bool isDouble = arr->IsDouble();

        for (size_t i = 0; i < len; i++) {
            double value;
            if (specialized) {
                int64_t bits = arr->Get(i);
                if (isDouble) {
                    value = *(double*)&bits;
                } else {
                    value = (double)bits;
                }
            } else {
                value = ts_value_get_double((TsValue*)arr->Get(i));
            }

            // Store based on element size
            switch (elementSize) {
                case 1:
                    data[i] = (uint8_t)(int8_t)value;
                    break;
                case 2:
                    ((int16_t*)data)[i] = (int16_t)value;
                    break;
                case 4:
                    ((int32_t*)data)[i] = (int32_t)value;
                    break;
                case 8:
                    ((double*)data)[i] = value;
                    break;
            }
        }
        return ta;
    }

    // Uint8ClampedArray from array
    void* ts_typed_array_from_array_clamped(void* array) {
        if (!array) return nullptr;
        TsArray* arr = (TsArray*)array;
        size_t len = (size_t)arr->Length();
        TsTypedArray* ta = TsTypedArray::Create(len, 1, true, TypedArrayType::Uint8Clamped);
        uint8_t* data = ta->GetData();
        bool specialized = arr->IsSpecialized();
        bool isDouble = arr->IsDouble();

        for (size_t i = 0; i < len; i++) {
            double value;
            if (specialized) {
                int64_t bits = arr->Get(i);
                if (isDouble) {
                    value = *(double*)&bits;
                } else {
                    value = (double)bits;
                }
            } else {
                value = ts_value_get_double((TsValue*)arr->Get(i));
            }

            // Clamp to 0-255
            if (value < 0) value = 0;
            else if (value > 255) value = 255;
            data[i] = (uint8_t)value;
        }
        return ta;
    }

    void* ts_data_view_create(void* buffer) {
        return TsDataView::Create((TsBuffer*)buffer);
    }

    int64_t DataView_getUint32(void* context, void* dv, int64_t offset, bool littleEndian) {
        if (!dv) return 0;
        TsDataView* view = (TsDataView*)dv;
        TsBuffer* buf = view->GetBuffer();
        if (!buf || (size_t)offset + 4 > buf->GetLength()) return 0;
        
        uint32_t val;
        std::memcpy(&val, buf->GetData() + offset, 4);
        if (littleEndian) {
            // Assume host is little endian for now, or use byteswap if needed
            return (int64_t)val;
        } else {
            // Big endian
            uint32_t swapped = ((val & 0xFF000000) >> 24) |
                               ((val & 0x00FF0000) >> 8) |
                               ((val & 0x0000FF00) << 8) |
                               ((val & 0x000000FF) << 24);
            return (int64_t)swapped;
        }
    }

    void DataView_setUint32(void* context, void* dv, int64_t offset, int64_t value, bool littleEndian) {
        if (!dv) return;
        TsDataView* view = (TsDataView*)dv;
        TsBuffer* buf = view->GetBuffer();
        if (!buf || (size_t)offset + 4 > buf->GetLength()) return;

        uint32_t val = (uint32_t)value;
        if (!littleEndian) {
            val = ((val & 0xFF000000) >> 24) |
                  ((val & 0x00FF0000) >> 8) |
                  ((val & 0x0000FF00) << 8) |
                  ((val & 0x000000FF) << 24);
        }
        std::memcpy(buf->GetData() + offset, &val, 4);
    }

    // DataView getters - 8-bit (no endianness needed)
    int64_t DataView_getInt8(void* context, void* dv, int64_t offset, bool littleEndian) {
        if (!dv) return 0;
        TsDataView* view = (TsDataView*)dv;
        TsBuffer* buf = view->GetBuffer();
        if (!buf || (size_t)offset >= buf->GetLength()) return 0;
        return (int64_t)(int8_t)buf->GetData()[offset];
    }

    int64_t DataView_getUint8(void* context, void* dv, int64_t offset, bool littleEndian) {
        if (!dv) return 0;
        TsDataView* view = (TsDataView*)dv;
        TsBuffer* buf = view->GetBuffer();
        if (!buf || (size_t)offset >= buf->GetLength()) return 0;
        return (int64_t)buf->GetData()[offset];
    }

    // DataView getters - 16-bit
    int64_t DataView_getInt16(void* context, void* dv, int64_t offset, bool littleEndian) {
        if (!dv) return 0;
        TsDataView* view = (TsDataView*)dv;
        TsBuffer* buf = view->GetBuffer();
        if (!buf || (size_t)offset + 2 > buf->GetLength()) return 0;

        int16_t val;
        std::memcpy(&val, buf->GetData() + offset, 2);
        if (!littleEndian) {
            val = (int16_t)(((uint16_t)val >> 8) | ((uint16_t)val << 8));
        }
        return (int64_t)val;
    }

    int64_t DataView_getUint16(void* context, void* dv, int64_t offset, bool littleEndian) {
        if (!dv) return 0;
        TsDataView* view = (TsDataView*)dv;
        TsBuffer* buf = view->GetBuffer();
        if (!buf || (size_t)offset + 2 > buf->GetLength()) return 0;

        uint16_t val;
        std::memcpy(&val, buf->GetData() + offset, 2);
        if (!littleEndian) {
            val = (val >> 8) | (val << 8);
        }
        return (int64_t)val;
    }

    // DataView getters - 32-bit (getUint32 already exists above)
    int64_t DataView_getInt32(void* context, void* dv, int64_t offset, bool littleEndian) {
        if (!dv) return 0;
        TsDataView* view = (TsDataView*)dv;
        TsBuffer* buf = view->GetBuffer();
        if (!buf || (size_t)offset + 4 > buf->GetLength()) return 0;

        int32_t val;
        std::memcpy(&val, buf->GetData() + offset, 4);
        if (!littleEndian) {
            val = (int32_t)(((uint32_t)val >> 24) |
                           (((uint32_t)val & 0x00FF0000) >> 8) |
                           (((uint32_t)val & 0x0000FF00) << 8) |
                           ((uint32_t)val << 24));
        }
        return (int64_t)val;
    }

    // DataView float getters
    double DataView_getFloat32(void* context, void* dv, int64_t offset, bool littleEndian) {
        if (!dv) return 0;
        TsDataView* view = (TsDataView*)dv;
        TsBuffer* buf = view->GetBuffer();
        if (!buf || (size_t)offset + 4 > buf->GetLength()) return 0;

        uint32_t intVal;
        std::memcpy(&intVal, buf->GetData() + offset, 4);
        if (!littleEndian) {
            intVal = ((intVal >> 24) |
                     ((intVal & 0x00FF0000) >> 8) |
                     ((intVal & 0x0000FF00) << 8) |
                     (intVal << 24));
        }
        float floatVal;
        std::memcpy(&floatVal, &intVal, 4);
        return (double)floatVal;
    }

    double DataView_getFloat64(void* context, void* dv, int64_t offset, bool littleEndian) {
        if (!dv) return 0;
        TsDataView* view = (TsDataView*)dv;
        TsBuffer* buf = view->GetBuffer();
        if (!buf || (size_t)offset + 8 > buf->GetLength()) return 0;

        uint64_t intVal;
        std::memcpy(&intVal, buf->GetData() + offset, 8);
        if (!littleEndian) {
            intVal = ((intVal >> 56) |
                     ((intVal & 0x00FF000000000000ULL) >> 40) |
                     ((intVal & 0x0000FF0000000000ULL) >> 24) |
                     ((intVal & 0x000000FF00000000ULL) >> 8) |
                     ((intVal & 0x00000000FF000000ULL) << 8) |
                     ((intVal & 0x0000000000FF0000ULL) << 24) |
                     ((intVal & 0x000000000000FF00ULL) << 40) |
                     (intVal << 56));
        }
        double doubleVal;
        std::memcpy(&doubleVal, &intVal, 8);
        return doubleVal;
    }

    // DataView setters - 8-bit
    void DataView_setInt8(void* context, void* dv, int64_t offset, int64_t value, bool littleEndian) {
        if (!dv) return;
        TsDataView* view = (TsDataView*)dv;
        TsBuffer* buf = view->GetBuffer();
        if (!buf || (size_t)offset >= buf->GetLength()) return;
        buf->GetData()[offset] = (int8_t)value;
    }

    void DataView_setUint8(void* context, void* dv, int64_t offset, int64_t value, bool littleEndian) {
        if (!dv) return;
        TsDataView* view = (TsDataView*)dv;
        TsBuffer* buf = view->GetBuffer();
        if (!buf || (size_t)offset >= buf->GetLength()) return;
        buf->GetData()[offset] = (uint8_t)value;
    }

    // DataView setters - 16-bit
    void DataView_setInt16(void* context, void* dv, int64_t offset, int64_t value, bool littleEndian) {
        if (!dv) return;
        TsDataView* view = (TsDataView*)dv;
        TsBuffer* buf = view->GetBuffer();
        if (!buf || (size_t)offset + 2 > buf->GetLength()) return;

        int16_t val = (int16_t)value;
        if (!littleEndian) {
            val = (int16_t)(((uint16_t)val >> 8) | ((uint16_t)val << 8));
        }
        std::memcpy(buf->GetData() + offset, &val, 2);
    }

    void DataView_setUint16(void* context, void* dv, int64_t offset, int64_t value, bool littleEndian) {
        if (!dv) return;
        TsDataView* view = (TsDataView*)dv;
        TsBuffer* buf = view->GetBuffer();
        if (!buf || (size_t)offset + 2 > buf->GetLength()) return;

        uint16_t val = (uint16_t)value;
        if (!littleEndian) {
            val = (val >> 8) | (val << 8);
        }
        std::memcpy(buf->GetData() + offset, &val, 2);
    }

    // DataView setters - 32-bit (setUint32 already exists above)
    void DataView_setInt32(void* context, void* dv, int64_t offset, int64_t value, bool littleEndian) {
        if (!dv) return;
        TsDataView* view = (TsDataView*)dv;
        TsBuffer* buf = view->GetBuffer();
        if (!buf || (size_t)offset + 4 > buf->GetLength()) return;

        int32_t val = (int32_t)value;
        if (!littleEndian) {
            val = (int32_t)(((uint32_t)val >> 24) |
                           (((uint32_t)val & 0x00FF0000) >> 8) |
                           (((uint32_t)val & 0x0000FF00) << 8) |
                           ((uint32_t)val << 24));
        }
        std::memcpy(buf->GetData() + offset, &val, 4);
    }

    // DataView float setters
    void DataView_setFloat32(void* context, void* dv, int64_t offset, double value, bool littleEndian) {
        if (!dv) return;
        TsDataView* view = (TsDataView*)dv;
        TsBuffer* buf = view->GetBuffer();
        if (!buf || (size_t)offset + 4 > buf->GetLength()) return;

        float floatVal = (float)value;
        uint32_t intVal;
        std::memcpy(&intVal, &floatVal, 4);
        if (!littleEndian) {
            intVal = ((intVal >> 24) |
                     ((intVal & 0x00FF0000) >> 8) |
                     ((intVal & 0x0000FF00) << 8) |
                     (intVal << 24));
        }
        std::memcpy(buf->GetData() + offset, &intVal, 4);
    }

    void DataView_setFloat64(void* context, void* dv, int64_t offset, double value, bool littleEndian) {
        if (!dv) return;
        TsDataView* view = (TsDataView*)dv;
        TsBuffer* buf = view->GetBuffer();
        if (!buf || (size_t)offset + 8 > buf->GetLength()) return;

        uint64_t intVal;
        std::memcpy(&intVal, &value, 8);
        if (!littleEndian) {
            intVal = ((intVal >> 56) |
                     ((intVal & 0x00FF000000000000ULL) >> 40) |
                     ((intVal & 0x0000FF0000000000ULL) >> 24) |
                     ((intVal & 0x000000FF00000000ULL) >> 8) |
                     ((intVal & 0x00000000FF000000ULL) << 8) |
                     ((intVal & 0x0000000000FF0000ULL) << 24) |
                     ((intVal & 0x000000000000FF00ULL) << 40) |
                     (intVal << 56));
        }
        std::memcpy(buf->GetData() + offset, &intVal, 8);
    }

    // DataView properties
    void* DataView_getBuffer(void* context, void* dv) {
        if (!dv) return nullptr;
        TsDataView* view = (TsDataView*)dv;
        return view->GetBuffer();
    }

    int64_t DataView_getByteLength(void* context, void* dv) {
        if (!dv) return 0;
        TsDataView* view = (TsDataView*)dv;
        TsBuffer* buf = view->GetBuffer();
        return buf ? (int64_t)buf->GetLength() : 0;
    }

    int64_t DataView_getByteOffset(void* context, void* dv) {
        // DataView always starts at offset 0 for now
        return 0;
    }

    int64_t ts_typed_array_length(void* ta) {
        if (!ta) return 0;
        return (int64_t)((TsTypedArray*)ta)->GetLength();
    }

    // Generic element access - uses TsTypedArray::Get which handles all element sizes
    double ts_typed_array_get_generic(void* ta, int64_t index) {
        if (!ta) return 0;
        TsTypedArray* t = (TsTypedArray*)ta;
        return t->Get((size_t)index);
    }

    uint8_t ts_typed_array_get_u8(void* ta, int64_t index) {
        if (!ta) return 0;
        TsTypedArray* t = (TsTypedArray*)ta;
        if ((size_t)index >= t->GetLength()) return 0;
        return t->GetData()[index];
    }

    void ts_typed_array_set_u8(void* ta, int64_t index, uint8_t value) {
        if (!ta) return;
        TsTypedArray* t = (TsTypedArray*)ta;
        if ((size_t)index >= t->GetLength()) return;
        t->GetData()[index] = value;
    }

    uint32_t ts_typed_array_get_u32(void* ta, int64_t index) {
        if (!ta) return 0;
        TsTypedArray* t = (TsTypedArray*)ta;
        if ((size_t)index >= t->GetLength()) return 0;
        return ((uint32_t*)t->GetData())[index];
    }

    void ts_typed_array_set_u32(void* ta, int64_t index, uint32_t value) {
        if (!ta) return;
        TsTypedArray* t = (TsTypedArray*)ta;
        if ((size_t)index >= t->GetLength()) return;
        ((uint32_t*)t->GetData())[index] = value;
    }

    double ts_typed_array_get_f64(void* ta, int64_t index) {
        if (!ta) return 0;
        TsTypedArray* t = (TsTypedArray*)ta;
        if ((size_t)index >= t->GetLength()) return 0;
        return ((double*)t->GetData())[index];
    }

    void ts_typed_array_set_f64(void* ta, int64_t index, double value) {
        if (!ta) return;
        TsTypedArray* t = (TsTypedArray*)ta;
        if ((size_t)index >= t->GetLength()) return;
        ((double*)t->GetData())[index] = value;
    }
}

TsTypedArray* TsTypedArray::Create(size_t length, size_t elementSize, bool clamped, TypedArrayType type) {
    void* mem = ts_alloc(sizeof(TsTypedArray));
    return new(mem) TsTypedArray(length, elementSize, clamped, type);
}

TsTypedArray::TsTypedArray(size_t length, size_t elementSize, bool clamped, TypedArrayType type) {
    this->magic = MAGIC;  // Set inherited magic from TsObject
    this->length = length;
    this->elementSize = elementSize;
    this->clamped = clamped;
    this->arrayType = type;
    this->buffer = TsBuffer::Create(length * elementSize);
}

double TsTypedArray::Get(size_t index) {
    if (index >= length) return 0;
    uint8_t* data = buffer->GetData();
    // For clamped arrays (Uint8ClampedArray), read as unsigned
    if (clamped && elementSize == 1) {
        return (double)data[index];
    }
    switch (elementSize) {
        case 1: return (double)(int8_t)data[index];
        case 2: return (double)((int16_t*)data)[index];
        case 4: return (double)((int32_t*)data)[index];
        case 8: return ((double*)data)[index];
        default: return 0;
    }
}

void TsTypedArray::Set(size_t index, double value) {
    if (index >= length) return;
    uint8_t* data = buffer->GetData();
    // For clamped arrays (Uint8ClampedArray), clamp values to 0-255
    if (clamped && elementSize == 1) {
        if (value < 0) value = 0;
        else if (value > 255) value = 255;
        data[index] = (uint8_t)value;
        return;
    }
    switch (elementSize) {
        case 1: data[index] = (uint8_t)(int8_t)value; break;
        case 2: ((int16_t*)data)[index] = (int16_t)value; break;
        case 4: ((int32_t*)data)[index] = (int32_t)value; break;
        case 8: ((double*)data)[index] = value; break;
    }
}

TsDataView* TsDataView::Create(TsBuffer* buffer) {
    void* mem = ts_alloc(sizeof(TsDataView));
    return new(mem) TsDataView(buffer);
}

TsDataView::TsDataView(TsBuffer* buffer) {
    this->magic = MAGIC;  // Set inherited magic from TsObject
    this->buffer = buffer;
}
