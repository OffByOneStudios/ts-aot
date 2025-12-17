#pragma once

#include <cstdint>
#include <cstddef>

class TsArray {
public:
    static TsArray* Create(size_t initialCapacity = 4);
    static TsArray* CreateSized(size_t size);

    void Push(int64_t value);
    int64_t Get(size_t index);
    void Set(size_t index, int64_t value);
    int64_t Length();
    void Sort();

private:
    TsArray(size_t initialCapacity);

    int64_t* elements;
    size_t length;
    size_t capacity;
};

extern "C" {
    void* ts_array_create();
    void* ts_array_create_sized(int64_t size);
    void ts_array_push(void* arr, int64_t value);
    int64_t ts_array_get(void* arr, int64_t index);
    void ts_array_set(void* arr, int64_t index, int64_t value);
    int64_t ts_array_length(void* arr);
    void ts_array_sort(void* arr);
}
