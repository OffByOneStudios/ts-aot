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
    // For now, assume UTF-8
    char* utf8 = (char*)ts_alloc(length + 1);
    std::memcpy(utf8, data, length);
    utf8[length] = '\0';
    return TsString::Create(utf8);
}

extern "C" {
    void* ts_buffer_alloc(int64_t length) {
        return TsBuffer::Create((size_t)length);
    }

    void* ts_buffer_from(void* data) {
        // Handle string or array
        return nullptr; // TODO
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
        
        // Check if buf is a boxed TsValue* and unbox it
        TsValue* val = (TsValue*)buf;
        TsBuffer* buffer = nullptr;
        
        // Check for boxed TsValue (type field should be 0-10)
        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::OBJECT_PTR && val->ptr_val) {
                buffer = (TsBuffer*)val->ptr_val;
            } else {
                return nullptr;
            }
        } else {
            // Assume it's a raw TsBuffer*
            buffer = (TsBuffer*)buf;
        }
        
        return buffer->ToString((TsString*)encoding);
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
    this->length = length;
    this->buffer = TsBuffer::Create(length * elementSize);
}

TsDataView* TsDataView::Create(TsBuffer* buffer) {
    void* mem = ts_alloc(sizeof(TsDataView));
    return new(mem) TsDataView(buffer);
}

TsDataView::TsDataView(TsBuffer* buffer) {
    this->buffer = buffer;
}
