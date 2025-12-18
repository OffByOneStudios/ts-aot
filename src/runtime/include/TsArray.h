#pragma once

#include <cstdint>
#include <cstddef>

class TsArray {
public:
    static constexpr uint32_t MAGIC = 0x41525259; // "ARRY"
    static TsArray* Create(size_t initialCapacity = 4);
    static TsArray* CreateSized(size_t size);

    void Push(int64_t value);
    int64_t Get(size_t index);
    void Set(size_t index, int64_t value);
    int64_t Length();
    void Sort();
    int64_t IndexOf(int64_t value);
    void* Slice(int64_t start, int64_t end);
    void* Join(void* separator);

private:
    TsArray(size_t initialCapacity);

    uint32_t magic = MAGIC;
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
    void* ts_array_slice(void* arr, int64_t start, int64_t end);
    int64_t ts_array_indexOf(void* arr, int64_t value);
    void* ts_array_join(void* arr, void* separator);
    void ts_array_concat(void* arr, void* other);
}
