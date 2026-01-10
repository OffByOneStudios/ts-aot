#include "TsBuffer.h"
#include "GC.h"
#include "TsRuntime.h"
#include "TsArray.h"
#include <cstring>
#include <new>
#include <unicode/unistr.h>

TsBuffer* TsBuffer::Create(size_t length) {
    void* mem = ts_alloc(sizeof(TsBuffer));
    TsBuffer* buf = new(mem) TsBuffer(length);
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

TsBuffer::TsBuffer(size_t length) {
    this->magic = MAGIC;
    this->length = length;
    this->data = (uint8_t*)ts_alloc(length);
    std::memset(this->data, 0, length);
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

TsBuffer* TsBuffer::Slice(int64_t start, int64_t end) {
    // Normalize negative indices
    int64_t len = (int64_t)length;
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
    if (start < 0) start = len + start;
    if (end < 0) end = len;
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) return;
    
    std::memset(data + start, value, (size_t)(end - start));
}

int64_t TsBuffer::Copy(TsBuffer* target, int64_t targetStart, int64_t sourceStart, int64_t sourceEnd) {
    if (!target) return 0;
    
    int64_t srcLen = (int64_t)length;
    int64_t tgtLen = (int64_t)target->length;
    
    if (sourceEnd < 0) sourceEnd = srcLen;
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
    
    // Calculate total length if not provided
    size_t calcLen = 0;
    if (totalLength < 0) {
        for (size_t i = 0; i < count; i++) {
            TsBuffer* buf = (TsBuffer*)ts_value_get_object((TsValue*)arr->Get(i));
            if (!buf) buf = (TsBuffer*)(void*)arr->Get(i);
            if (buf && buf->magic == MAGIC) {
                calcLen += buf->length;
            }
        }
    } else {
        calcLen = (size_t)totalLength;
    }
    
    TsBuffer* result = Create(calcLen);
    size_t offset = 0;
    
    for (size_t i = 0; i < count && offset < calcLen; i++) {
        TsBuffer* buf = (TsBuffer*)ts_value_get_object((TsValue*)arr->Get(i));
        if (!buf) buf = (TsBuffer*)(void*)arr->Get(i);
        if (buf && buf->magic == MAGIC) {
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

extern "C" {
    void* ts_buffer_alloc(int64_t length) {
        return TsBuffer::Create((size_t)length);
    }

    void* ts_buffer_alloc_unsafe(int64_t length) {
        return TsBuffer::AllocUnsafe((size_t)length);
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
        return TsBuffer::FromString((TsString*)str, (TsString*)encoding);
    }

    void* ts_buffer_concat(void* list, int64_t totalLength) {
        return TsBuffer::Concat(list, totalLength);
    }

    int64_t ts_buffer_length(void* buf) {
        if (!buf) return 0;
        return (int64_t)((TsBuffer*)buf)->GetLength();
    }

    int64_t ts_buffer_byte_length(void* buf) {
        if (!buf) return 0;
        return (int64_t)((TsBuffer*)buf)->GetByteLength();
    }

    int64_t ts_buffer_byte_offset(void* buf) {
        if (!buf) return 0;
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

    void* ts_buffer_slice(void* buf, int64_t start, int64_t end) {
        if (!buf) return TsBuffer::Create(0);
        return ((TsBuffer*)buf)->Slice(start, end);
    }

    void* ts_buffer_subarray(void* buf, int64_t start, int64_t end) {
        if (!buf) return TsBuffer::Create(0);
        return ((TsBuffer*)buf)->Subarray(start, end);
    }

    void* ts_buffer_fill(void* buf, int64_t value, int64_t start, int64_t end) {
        if (!buf) return nullptr;
        TsBuffer* buffer = (TsBuffer*)buf;
        buffer->Fill((uint8_t)value, start, end);
        return buffer;  // Return this for chaining
    }

    int64_t ts_buffer_copy(void* source, void* target, int64_t targetStart, int64_t sourceStart, int64_t sourceEnd) {
        if (!source || !target) return 0;
        return ((TsBuffer*)source)->Copy((TsBuffer*)target, targetStart, sourceStart, sourceEnd);
    }

    bool ts_buffer_is_buffer(void* obj) {
        if (!obj) return false;
        
        // Try to unbox if it's a TsValue*
        void* raw = ts_value_get_object((TsValue*)obj);
        if (raw) {
            obj = raw;
        }
        
        TsBuffer* buf = (TsBuffer*)obj;
        return buf->magic == TsBuffer::MAGIC;
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
        return ts_value_make_double(((TsBuffer*)buf)->GetLength());
    }

    void* ts_typed_array_create_u8(int64_t length) {
        return TsTypedArray::Create((size_t)length, 1);
    }

    void* ts_typed_array_create_u32(int64_t length) {
        return TsTypedArray::Create((size_t)length, 4);
    }

    void* ts_typed_array_create_f64(int64_t length) {
        return TsTypedArray::Create((size_t)length, 8);
    }

    void* ts_typed_array_from_array_u8(void* array) {
        if (!array) return nullptr;
        TsArray* arr = (TsArray*)array;
        size_t len = (size_t)arr->Length();
        TsTypedArray* ta = TsTypedArray::Create(len, 1);
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
        TsTypedArray* ta = TsTypedArray::Create(len, 4);
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
        TsTypedArray* ta = TsTypedArray::Create(len, 8);
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
        return TsTypedArray::Create((size_t)length, (size_t)elementSize);
    }

    // Generic typed array from array with element size parameter
    void* ts_typed_array_from_array(void* array, int32_t elementSize) {
        if (!array) return nullptr;
        TsArray* arr = (TsArray*)array;
        size_t len = (size_t)arr->Length();
        TsTypedArray* ta = TsTypedArray::Create(len, (size_t)elementSize);
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

    int64_t ts_typed_array_length(void* ta) {
        if (!ta) return 0;
        return (int64_t)((TsTypedArray*)ta)->GetLength();
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

TsTypedArray* TsTypedArray::Create(size_t length, size_t elementSize) {
    void* mem = ts_alloc(sizeof(TsTypedArray));
    return new(mem) TsTypedArray(length, elementSize);
}

TsTypedArray::TsTypedArray(size_t length, size_t elementSize) {
    this->magic = MAGIC;  // Set inherited magic from TsObject
    this->length = length;
    this->elementSize = elementSize;
    this->buffer = TsBuffer::Create(length * elementSize);
}

double TsTypedArray::Get(size_t index) {
    if (index >= length) return 0;
    uint8_t* data = buffer->GetData();
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
    this->buffer = buffer;
}
