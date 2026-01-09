#pragma once

#include <cstdint>
#include <cstddef>
#include "TsObject.h"

class TsArray {
public:
    static constexpr uint32_t MAGIC = 0x41525259; // "ARRY"
    static TsArray* Create(size_t initialCapacity = 4);
    static TsArray* CreateSized(size_t size);
    static TsArray* CreateSpecialized(size_t size, size_t elementSize, bool isDouble = false);

    void Push(int64_t value);
    int64_t Pop();
    void Unshift(int64_t value);
    int64_t Shift();
    int64_t Get(size_t index);
    int64_t GetUnchecked(size_t index) { return ((int64_t*)elements)[index]; }
    void Set(size_t index, int64_t value);
    void SetUnchecked(size_t index, int64_t value) { ((int64_t*)elements)[index] = value; }
    int64_t Length();
    void Sort();
    int64_t IndexOf(int64_t value);
    int64_t LastIndexOf(int64_t value);
    bool Includes(int64_t value);
    void Remove(int64_t value);
    int64_t At(int64_t index);
    void* Slice(int64_t start, int64_t end);
    void* Join(void* separator);
    void* Flat(int64_t depth = 1);
    void* FlatMap(void* callback, void* thisArg = nullptr);
    void Reverse();

    // ES2023 "change array by copy" methods
    TsArray* ToReversed();
    TsArray* ToSorted();
    TsArray* ToSpliced(int64_t start, int64_t deleteCount, void* items, int64_t itemCount);
    TsArray* With(int64_t index, int64_t value);

    void* GetElementsPtr() { return elements; }
    bool IsSpecialized() { return isSpecialized; }
    bool IsDouble() { return isDouble; }
    struct TaggedValue* GetElementBoxed(size_t index);  // Get element as boxed TsValue*

    void ForEach(void* callback, void* thisArg = nullptr);
    void* Map(void* callback, void* thisArg = nullptr);
    void* Filter(void* callback, void* thisArg = nullptr);
    void* Reduce(void* callback, void* initialValue = nullptr);
    void* ReduceRight(void* callback, void* initialValue = nullptr);
    bool Some(void* callback, void* thisArg = nullptr);
    bool Every(void* callback, void* thisArg = nullptr);
    struct TaggedValue* Find(void* callback, void* thisArg = nullptr);
    int64_t FindIndex(void* callback, void* thisArg = nullptr);
    struct TaggedValue* FindLast(void* callback, void* thisArg = nullptr);
    int64_t FindLastIndex(void* callback, void* thisArg = nullptr);

private:
    TsArray(size_t initialCapacity, size_t elementSize = 8);

    uint32_t magic = MAGIC;
    void* elements;
    size_t length;
    size_t capacity;
    size_t elementSize;
    bool isSpecialized = false;
    bool isDouble = false;
};

extern "C" {
    void* ts_array_create();
    void* ts_array_create_sized(int64_t size);
    void* ts_array_create_specialized(int64_t size, int64_t elementSize, bool isDouble);
    void* ts_array_get_elements_ptr(void* arr);
    void ts_array_push(void* arr, void* value);
    void* ts_array_pop(void* arr);
    void ts_array_unshift(void* arr, void* value);
    void* ts_array_shift(void* arr);
    
    TsValue* ts_array_get_as_value(void* arr, int64_t index);
    void* ts_array_get_unchecked(void* arr, int64_t index);
    void ts_array_set_unchecked(void* arr, int64_t index, void* value);
    
    // Value-based API variants - avoid heap allocation
    TsValue ts_array_get_v(void* arr, int64_t index);
    void ts_array_set_v(void* arr, int64_t index, TsValue value);
    int64_t ts_array_length(void* arr);
    void ts_array_sort(void* arr);
    void ts_array_sort_with_comparator(void* arr, void* comparator);
    void* ts_array_slice(void* arr, int64_t start, int64_t end);
    void* ts_array_flat(void* arr, int64_t depth);
    void* ts_array_flatMap(void* arr, void* callback, void* thisArg);
    int64_t ts_array_indexOf(void* arr, int64_t value);
    int64_t ts_array_lastIndexOf(void* arr, int64_t value);
    bool ts_array_includes(void* arr, int64_t value);
    void* ts_array_at(void* arr, int64_t index);
    void* ts_array_join(void* arr, void* separator);
    void ts_array_concat(void* arr, void* other);
    void ts_array_reverse(void* arr);
    void ts_array_forEach(void* arr, void* callback, void* thisArg);
    void* ts_array_map(void* arr, void* callback, void* thisArg);
    void* ts_array_filter(void* arr, void* callback, void* thisArg);
    void* ts_array_reduce(void* arr, void* callback, void* initialValue);
    void* ts_array_reduceRight(void* arr, void* callback, void* initialValue);
    bool ts_array_some(void* arr, void* callback, void* thisArg);
    bool ts_array_every(void* arr, void* callback, void* thisArg);
    struct TaggedValue* ts_array_find(void* arr, void* callback, void* thisArg);
    int64_t ts_array_findIndex(void* arr, void* callback, void* thisArg);
    struct TaggedValue* ts_array_findLast(void* arr, void* callback, void* thisArg);
    int64_t ts_array_findLastIndex(void* arr, void* callback, void* thisArg);
    bool ts_array_is_array(void* value);
    void* ts_array_from(void* arrayLike, void* mapFn, void* thisArg);
    void* ts_array_fill(void* arr, void* value, int64_t start, int64_t end);

    // ES2023 "change array by copy" methods
    void* ts_array_toReversed(void* arr);
    void* ts_array_toSorted(void* arr);
    void* ts_array_toSpliced(void* arr, int64_t start, int64_t deleteCount, void* items, int64_t itemCount);
    void* ts_array_with(void* arr, int64_t index, void* value);

    // Value-based API variants
    void ts_array_set_v(void* arr, int64_t index, TsValue value);
    TsValue ts_array_get_v(void* arr, int64_t index);
    
    // Inline IR helpers - scalar-based API to avoid struct passing
    void __ts_array_get_inline(void* arr, int64_t index, uint8_t* out_type, int64_t* out_value);
    void __ts_array_set_inline(void* arr, int64_t index, uint8_t val_type, int64_t val_value);
    int64_t __ts_array_length(void* arr);
}
