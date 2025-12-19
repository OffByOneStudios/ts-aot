#include "TsArray.h"
#include "GC.h"
#include <cstring>
#include <iostream>
#include <new>
#include <algorithm>

TsArray* TsArray::Create(size_t initialCapacity) {
    void* mem = ts_alloc(sizeof(TsArray));
    return new(mem) TsArray(initialCapacity);
}

TsArray* TsArray::CreateSized(size_t size) {
    void* mem = ts_alloc(sizeof(TsArray));
    TsArray* arr = new(mem) TsArray(size);
    arr->length = size;
    std::memset(arr->elements, 0, size * sizeof(int64_t));
    return arr;
}

TsArray::TsArray(size_t initialCapacity) {
    this->capacity = initialCapacity > 0 ? initialCapacity : 4;
    this->length = 0;
    this->elements = (int64_t*)ts_alloc(this->capacity * sizeof(int64_t));
}

void TsArray::Push(int64_t value) {
    if (length >= capacity) {
        size_t newCapacity = capacity * 2;
        int64_t* newElements = (int64_t*)ts_alloc(newCapacity * sizeof(int64_t));
        std::memcpy(newElements, elements, length * sizeof(int64_t));
        elements = newElements;
        capacity = newCapacity;
    }
    elements[length++] = value;
}

int64_t TsArray::Pop() {
    if (length == 0) return 0;
    return elements[--length];
}

void TsArray::Unshift(int64_t value) {
    if (length >= capacity) {
        size_t newCapacity = capacity * 2;
        int64_t* newElements = (int64_t*)ts_alloc(newCapacity * sizeof(int64_t));
        std::memcpy(newElements + 1, elements, length * sizeof(int64_t));
        elements = newElements;
        capacity = newCapacity;
    } else {
        std::memmove(elements + 1, elements, length * sizeof(int64_t));
    }
    elements[0] = value;
    length++;
}

int64_t TsArray::Shift() {
    if (length == 0) return 0;
    int64_t result = elements[0];
    std::memmove(elements, elements + 1, (length - 1) * sizeof(int64_t));
    length--;
    return result;
}

int64_t TsArray::Get(size_t index) {
    if (index >= length) {
        std::cerr << "Array index out of bounds: " << index << std::endl;
        return 0;
    }
    return elements[index];
}

void TsArray::Set(size_t index, int64_t value) {
    if (index >= length) {
        if (index == length) {
            Push(value);
            return;
        }
        std::cerr << "Array index out of bounds: " << index << std::endl;
        return;
    }
    elements[index] = value;
}

int64_t TsArray::Length() {
    return length;
}

void TsArray::Sort() {
    std::sort(elements, elements + length);
}

int64_t TsArray::IndexOf(int64_t value) {
    for (size_t i = 0; i < length; ++i) {
        if (elements[i] == value) return (int64_t)i;
    }
    return -1;
}

bool TsArray::Includes(int64_t value) {
    return IndexOf(value) != -1;
}

int64_t TsArray::At(int64_t index) {
    if (index < 0) index = length + index;
    if (index < 0 || index >= (int64_t)length) return 0;
    return elements[index];
}

void* TsArray::Slice(int64_t start, int64_t end) {
    if (start < 0) start = length + start;
    if (start < 0) start = 0;
    if (start > (int64_t)length) start = length;

    if (end < 0) end = length + end;
    if (end > (int64_t)length) end = length;
    if (end < start) end = start;

    size_t newLength = end - start;
    TsArray* result = TsArray::Create(newLength);
    for (size_t i = 0; i < newLength; ++i) {
        result->Push(elements[start + i]);
    }
    return result;
}

#include "TsString.h"
#include <sstream>

void* TsArray::Join(void* separator) {
    TsString* sep = (TsString*)separator;
    const char* sepStr = sep ? sep->ToUtf8() : ",";
    
    std::stringstream ss;
    for (size_t i = 0; i < length; ++i) {
        if (i > 0) ss << sepStr;
        
        // This is tricky because elements can be anything (int, double, pointer to TsString)
        // For now, assume they are either ints or pointers to TsString
        // We need a way to check if it's a pointer to TsString
        int64_t val = elements[i];
        if (val == 0) {
            ss << "null";
        } else if (val > 0x1000) { // Heuristic for pointer
            // Check magic
            uint32_t* magicPtr = (uint32_t*)val;
            if (*magicPtr == TsString::MAGIC) {
                ss << ((TsString*)val)->ToUtf8();
            } else if (*magicPtr == TsArray::MAGIC) {
                ss << "[Array]";
            } else {
                ss << val;
            }
        } else {
            ss << val;
        }
    }
    return TsString::Create(ss.str().c_str());
}

extern "C" {
    void* ts_array_create() {
        return TsArray::Create();
    }

    void* ts_array_create_sized(int64_t size) {
        return TsArray::CreateSized((size_t)size);
    }

    void ts_array_push(void* arr, void* value) {
        ((TsArray*)arr)->Push((int64_t)value);
    }

    void* ts_array_pop(void* arr) {
        return (void*)((TsArray*)arr)->Pop();
    }

    void ts_array_unshift(void* arr, void* value) {
        ((TsArray*)arr)->Unshift((int64_t)value);
    }

    void* ts_array_shift(void* arr) {
        return (void*)((TsArray*)arr)->Shift();
    }

    void* ts_array_get(void* arr, int64_t index) {
        return (void*)((TsArray*)arr)->Get(index);
    }

    void ts_array_set(void* arr, int64_t index, void* value) {
        ((TsArray*)arr)->Set(index, (int64_t)value);
    }

    int64_t ts_array_length(void* arr) {
        return ((TsArray*)arr)->Length();
    }

    void ts_array_sort(void* arr) {
        ((TsArray*)arr)->Sort();
    }

    void* ts_array_slice(void* arr, int64_t start, int64_t end) {
        return ((TsArray*)arr)->Slice(start, end);
    }

    int64_t ts_array_indexOf(void* arr, int64_t value) {
        return ((TsArray*)arr)->IndexOf(value);
    }

    bool ts_array_includes(void* arr, int64_t value) {
        return ((TsArray*)arr)->Includes(value);
    }

    void* ts_array_at(void* arr, int64_t index) {
        return (void*)((TsArray*)arr)->At(index);
    }

    void* ts_array_join(void* arr, void* separator) {
        return ((TsArray*)arr)->Join(separator);
    }

    void ts_array_concat(void* arr, void* other) {
        TsArray* target = (TsArray*)arr;
        TsArray* source = (TsArray*)other;
        for (size_t i = 0; i < source->Length(); ++i) {
            target->Push(source->Get(i));
        }
    }
}
