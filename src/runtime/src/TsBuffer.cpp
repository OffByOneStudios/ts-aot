#include "TsBuffer.h"
#include "GC.h"
#include "TsRuntime.h"
#include <cstring>
#include <new>
#include <unicode/unistr.h>

TsBuffer* TsBuffer::Create(size_t length) {
    void* mem = ts_alloc(sizeof(TsBuffer));
    return new(mem) TsBuffer(length);
}

TsBuffer* TsBuffer::FromString(TsString* str, TsString* encoding) {
    if (!str) return nullptr;
    const char* utf8 = str->ToUtf8();
    size_t len = std::strlen(utf8);
    TsBuffer* buf = Create(len);
    std::memcpy(buf->data, utf8, len);
    return buf;
}

TsBuffer::TsBuffer(size_t length) {
    this->length = length;
    this->data = (uint8_t*)ts_alloc(length);
    std::memset(this->data, 0, length);
}

uint8_t TsBuffer::Get(size_t index) {
    if (index >= length) return 0;
    return data[index];
}

void TsBuffer::Set(size_t index, uint8_t value) {
    if (index < length) {
        data[index] = value;
    }
}

TsString* TsBuffer::ToString(TsString* encoding) {
    // For now, assume UTF-8
    char* utf8 = (char*)ts_alloc(length + 1);
    std::memcpy(utf8, data, length);
    utf8[length] = '\0';
    return TsString::Create(utf8);
}

extern "C" {
    void* ts_buffer_alloc(void* vtable, int64_t length) {
        TsBuffer* buf = TsBuffer::Create((size_t)length);
        buf->vtable = vtable;
        return buf;
    }

    void* ts_buffer_from_string(void* vtable, void* str, void* encoding) {
        TsBuffer* buf = TsBuffer::FromString((TsString*)str, (TsString*)encoding);
        if (buf) buf->vtable = vtable;
        return buf;
    }

    int64_t ts_buffer_length(void* buf) {
        if (!buf) return 0;
        return (int64_t)((TsBuffer*)buf)->GetLength();
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
        return ((TsBuffer*)buf)->ToString((TsString*)encoding);
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
}
