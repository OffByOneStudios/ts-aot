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

extern "C" {
    void* ts_array_create() {
        return TsArray::Create();
    }

    void* ts_array_create_sized(int64_t size) {
        return TsArray::CreateSized((size_t)size);
    }

    void ts_array_push(void* arr, int64_t value) {
        ((TsArray*)arr)->Push(value);
    }

    int64_t ts_array_get(void* arr, int64_t index) {
        return ((TsArray*)arr)->Get(index);
    }

    void ts_array_set(void* arr, int64_t index, int64_t value) {
        ((TsArray*)arr)->Set(index, value);
    }

    int64_t ts_array_length(void* arr) {
        return ((TsArray*)arr)->Length();
    }

    void ts_array_sort(void* arr) {
        ((TsArray*)arr)->Sort();
    }

    void* ts_array_slice(void* arr, int64_t start) {
        TsArray* source = (TsArray*)arr;
        if (start < 0) start = 0;
        if (start > (int64_t)source->Length()) start = source->Length();
        
        size_t newLength = source->Length() - start;
        TsArray* result = TsArray::Create(newLength);
        for (size_t i = 0; i < newLength; ++i) {
            result->Push(source->Get(start + i));
        }
        return result;
    }
}
