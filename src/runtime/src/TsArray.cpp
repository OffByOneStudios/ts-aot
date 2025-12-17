#include "TsArray.h"
#include "GC.h"
#include <cstring>
#include <iostream>
#include <new>

TsArray* TsArray::Create(size_t initialCapacity) {
    void* mem = ts_alloc(sizeof(TsArray));
    return new(mem) TsArray(initialCapacity);
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

extern "C" {
    void* ts_array_create() {
        return TsArray::Create();
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
}
