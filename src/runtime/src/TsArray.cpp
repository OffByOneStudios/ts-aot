#include "TsArray.h"
#include "TsObject.h"
#include "TsRuntime.h"
#include "GC.h"
#include <cstring>
#include <iostream>
#include <new>
#include <algorithm>

TsArray* TsArray::Create(size_t initialCapacity) {
    void* mem = ts_alloc(sizeof(TsArray));
    return new(mem) TsArray(initialCapacity, 8);
}

TsArray* TsArray::CreateSized(size_t size) {
    void* mem = ts_alloc(sizeof(TsArray));
    TsArray* arr = new(mem) TsArray(size, 8);
    arr->length = size;
    std::memset(arr->elements, 0, size * 8);
    return arr;
}

TsArray* TsArray::CreateSpecialized(size_t size, size_t elementSize, bool isDouble) {
    void* mem = ts_alloc(sizeof(TsArray));
    TsArray* arr = new(mem) TsArray(size, elementSize);
    arr->length = size;
    arr->isSpecialized = true;
    arr->isDouble = isDouble;
    std::memset(arr->elements, 0, size * elementSize);
    return arr;
}

TsArray::TsArray(size_t initialCapacity, size_t elementSize) {
    this->capacity = initialCapacity > 0 ? initialCapacity : 4;
    this->length = 0;
    this->elementSize = elementSize;
    this->elements = ts_alloc(this->capacity * this->elementSize);
}

void TsArray::Push(int64_t value) {
    if (elementSize != 8) {
        std::cerr << "Push not supported on specialized arrays yet" << std::endl;
        return;
    }
    if (length >= capacity) {
        size_t newCapacity = capacity * 2;
        void* newElements = ts_alloc(newCapacity * elementSize);
        std::memcpy(newElements, elements, length * elementSize);
        elements = newElements;
        capacity = newCapacity;
    }
    ((int64_t*)elements)[length++] = value;
}

int64_t TsArray::Pop() {
    if (length == 0) return 0;
    if (elementSize != 8) return 0;
    return ((int64_t*)elements)[--length];
}

void TsArray::Unshift(int64_t value) {
    if (elementSize != 8) return;
    if (length >= capacity) {
        size_t newCapacity = capacity * 2;
        int64_t* newElements = (int64_t*)ts_alloc(newCapacity * 8);
        std::memcpy(newElements + 1, elements, length * 8);
        elements = newElements;
        capacity = newCapacity;
    } else {
        std::memmove((int64_t*)((int64_t*)elements) + 1, elements, length * 8);
    }
    ((int64_t*)elements)[0] = value;
    length++;
}

int64_t TsArray::Shift() {
    if (length == 0) return 0;
    if (elementSize != 8) return 0;
    int64_t result = ((int64_t*)elements)[0];
    std::memmove(elements, (int64_t*)((int64_t*)elements) + 1, (length - 1) * 8);
    length--;
    return result;
}

int64_t TsArray::Get(size_t index) {
    if (index >= length) {
        // JavaScript behavior: return undefined (0) for out-of-bounds access
        return 0;
    }
    if (elementSize != 8) return 0;
    return ((int64_t*)elements)[index];
}

void TsArray::Set(size_t index, int64_t value) {
    if (index >= length) {
        if (index == length) {
            Push(value);
            return;
        }
        ts_panic("Array index out of bounds");
    }
    if (elementSize != 8) return;
    ((int64_t*)elements)[index] = value;
}

int64_t TsArray::Length() {
    return length;
}

void TsArray::Sort() {
    std::sort((int64_t*)elements, ((int64_t*)elements) + length);
}

int64_t TsArray::IndexOf(int64_t value) {
    for (size_t i = 0; i < length; ++i) {
        if (((int64_t*)elements)[i] == value) return (int64_t)i;
    }
    return -1;
}

bool TsArray::Includes(int64_t value) {
    return IndexOf(value) != -1;
}

void TsArray::Remove(int64_t value) {
    int64_t index = IndexOf(value);
    if (index == -1) return;
    
    if (index < (int64_t)length - 1) {
        std::memmove((int64_t*)elements + index, (int64_t*)elements + index + 1, (length - index - 1) * 8);
    }
    length--;
}

void* TsArray::Flat(int64_t depth) {
    TsArray* result = TsArray::Create();
    for (size_t i = 0; i < length; ++i) {
        int64_t val = ((int64_t*)elements)[i];
        if (depth > 0 && val > 0x1000 && !((val & 0xFFFF000000000000) == 0x7FF8000000000000)) {
            // Heuristic for pointer, and not a NaN-boxed double
            uint32_t* magicPtr = (uint32_t*)val;
            if (*magicPtr == TsArray::MAGIC) {
                TsArray* sub = (TsArray*)val;
                TsArray* flattenedSub = (TsArray*)sub->Flat(depth - 1);
                for (size_t j = 0; j < flattenedSub->length; ++j) {
                    result->Push(((int64_t*)flattenedSub->elements)[j]);
                }
                continue;
            }
        }
        result->Push(val);
    }
    return result;
}

void* TsArray::FlatMap(void* callback, void* thisArg) {
    TsArray* mapped = (TsArray*)Map(callback, thisArg);
    return mapped->Flat(1);
}

void TsArray::ForEach(void* callback, void* thisArg) {
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal || cbVal->type != ValueType::OBJECT_PTR) return;
    TsFunction* func = (TsFunction*)cbVal->ptr_val;
    auto fp = (TsValue* (*)(TsValue*, TsValue*, TsValue*))func->funcPtr;

    for (size_t i = 0; i < length; ++i) {
        TsValue* v = (((int64_t*)elements)[i] > 0xFFFFFFFF || ((int64_t*)elements)[i] < 0) ? ts_value_make_object((void*)((int64_t*)elements)[i]) : ts_value_make_int(((int64_t*)elements)[i]);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        fp(v, idx, arr);
    }
}

void* TsArray::Map(void* callback, void* thisArg) {
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal || cbVal->type != ValueType::OBJECT_PTR) return nullptr;
    TsFunction* func = (TsFunction*)cbVal->ptr_val;
    auto fp = (TsValue* (*)(TsValue*, TsValue*, TsValue*))func->funcPtr;

    TsArray* result = TsArray::Create(length);
    for (size_t i = 0; i < length; ++i) {
        TsValue* v = (((int64_t*)elements)[i] > 0xFFFFFFFF || ((int64_t*)elements)[i] < 0) ? ts_value_make_object((void*)((int64_t*)elements)[i]) : ts_value_make_int(((int64_t*)elements)[i]);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = fp(v, idx, arr);
        
        if (res->type == ValueType::NUMBER_INT) result->Push(res->i_val);
        else if (res->type == ValueType::NUMBER_DBL) result->Push(*(int64_t*)&res->d_val);
        else result->Push((int64_t)res->ptr_val);
    }
    return result;
}

void* TsArray::Filter(void* callback, void* thisArg) {
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal || cbVal->type != ValueType::OBJECT_PTR) return nullptr;
    TsFunction* func = (TsFunction*)cbVal->ptr_val;
    auto fp = (TsValue* (*)(TsValue*, TsValue*, TsValue*))func->funcPtr;

    TsArray* result = TsArray::Create();
    for (size_t i = 0; i < length; ++i) {
        TsValue* v = (((int64_t*)elements)[i] > 0xFFFFFFFF || ((int64_t*)elements)[i] < 0) ? ts_value_make_object((void*)((int64_t*)elements)[i]) : ts_value_make_int(((int64_t*)elements)[i]);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = fp(v, idx, arr);
        
        if (res->type == ValueType::BOOLEAN && res->b_val) {
            result->Push(((int64_t*)elements)[i]);
        }
    }
    return result;
}

void* TsArray::Reduce(void* callback, void* initialValue) {
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal || cbVal->type != ValueType::OBJECT_PTR) return nullptr;
    TsFunction* func = (TsFunction*)cbVal->ptr_val;
    auto fp = (TsValue* (*)(TsValue*, TsValue*, TsValue*, TsValue*))func->funcPtr;

    TsValue* accumulator = (TsValue*)initialValue;
    size_t startIdx = 0;
    if (!accumulator && length > 0) {
        accumulator = (((int64_t*)elements)[0] > 0xFFFFFFFF || ((int64_t*)elements)[0] < 0) ? ts_value_make_object((void*)((int64_t*)elements)[0]) : ts_value_make_int(((int64_t*)elements)[0]);
        startIdx = 1;
    }

    for (size_t i = startIdx; i < length; ++i) {
        TsValue* v = (((int64_t*)elements)[i] > 0xFFFFFFFF || ((int64_t*)elements)[i] < 0) ? ts_value_make_object((void*)((int64_t*)elements)[i]) : ts_value_make_int(((int64_t*)elements)[i]);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        accumulator = fp(accumulator, v, idx, arr);
    }
    return accumulator;
}

bool TsArray::Some(void* callback, void* thisArg) {
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal || cbVal->type != ValueType::OBJECT_PTR) return false;
    TsFunction* func = (TsFunction*)cbVal->ptr_val;
    auto fp = (TsValue* (*)(TsValue*, TsValue*, TsValue*))func->funcPtr;

    for (size_t i = 0; i < length; ++i) {
        TsValue* v = (((int64_t*)elements)[i] > 0xFFFFFFFF || ((int64_t*)elements)[i] < 0) ? ts_value_make_object((void*)((int64_t*)elements)[i]) : ts_value_make_int(((int64_t*)elements)[i]);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = fp(v, idx, arr);
        if (res->type == ValueType::BOOLEAN && res->b_val) return true;
    }
    return false;
}

bool TsArray::Every(void* callback, void* thisArg) {
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal || cbVal->type != ValueType::OBJECT_PTR) return false;
    TsFunction* func = (TsFunction*)cbVal->ptr_val;
    auto fp = (TsValue* (*)(TsValue*, TsValue*, TsValue*))func->funcPtr;

    for (size_t i = 0; i < length; ++i) {
        TsValue* v = (((int64_t*)elements)[i] > 0xFFFFFFFF || ((int64_t*)elements)[i] < 0) ? ts_value_make_object((void*)((int64_t*)elements)[i]) : ts_value_make_int(((int64_t*)elements)[i]);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = fp(v, idx, arr);
        if (res->type == ValueType::BOOLEAN && !res->b_val) return false;
    }
    return true;
}

TsValue* TsArray::Find(void* callback, void* thisArg) {
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal || cbVal->type != ValueType::OBJECT_PTR) return ts_value_make_undefined();
    TsFunction* func = (TsFunction*)cbVal->ptr_val;
    auto fp = (TsValue* (*)(TsValue*, TsValue*, TsValue*))func->funcPtr;

    for (size_t i = 0; i < length; ++i) {
        TsValue* v = (((int64_t*)elements)[i] > 0xFFFFFFFF || ((int64_t*)elements)[i] < 0) ? ts_value_make_object((void*)((int64_t*)elements)[i]) : ts_value_make_int(((int64_t*)elements)[i]);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = fp(v, idx, arr);
        if (res->type == ValueType::BOOLEAN && res->b_val) {
            return (((int64_t*)elements)[i] > 0xFFFFFFFF || ((int64_t*)elements)[i] < 0) ? ts_value_make_object((void*)((int64_t*)elements)[i]) : ts_value_make_int(((int64_t*)elements)[i]);
        }
    }
    return ts_value_make_undefined();
}

int64_t TsArray::FindIndex(void* callback, void* thisArg) {
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal || cbVal->type != ValueType::OBJECT_PTR) return -1;
    TsFunction* func = (TsFunction*)cbVal->ptr_val;
    auto fp = (TsValue* (*)(TsValue*, TsValue*, TsValue*))func->funcPtr;

    for (size_t i = 0; i < length; ++i) {
        TsValue* v = (((int64_t*)elements)[i] > 0xFFFFFFFF || ((int64_t*)elements)[i] < 0) ? ts_value_make_object((void*)((int64_t*)elements)[i]) : ts_value_make_int(((int64_t*)elements)[i]);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = fp(v, idx, arr);
        if (res->type == ValueType::BOOLEAN && res->b_val) return (int64_t)i;
    }
    return -1;
}

int64_t TsArray::At(int64_t index) {
    if (index < 0) index = length + index;
    if (index < 0 || index >= (int64_t)length) return 0;
    return ((int64_t*)elements)[index];
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
        result->Push(((int64_t*)elements)[start + i]);
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
        int64_t val = ((int64_t*)elements)[i];
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

    void* ts_array_create_specialized(int64_t size, int64_t elementSize, bool isDouble) {
        return TsArray::CreateSpecialized((size_t)size, (size_t)elementSize, isDouble);
    }

    void* ts_array_get_elements_ptr(void* arr) {
        return ((TsArray*)arr)->GetElementsPtr();
    }

    bool ts_array_is_specialized(void* arr) {
        if (!arr) return false;
        return ((TsArray*)arr)->IsSpecialized();
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

    TsValue* ts_array_get_as_value(void* arr, int64_t index) {
        if (!arr) {
            // printf("ts_array_get_as_value: arr is null\n");
            return ts_value_make_undefined();
        }
        TsArray* array = (TsArray*)arr;
        // printf("ts_array_get_as_value: arr=%p, index=%lld\n", arr, index);
        if (index < 0 || index >= array->Length()) {
             return ts_value_make_undefined();
        }
        int64_t val = array->Get(index);
        if (array->IsSpecialized()) {
            if (array->IsDouble()) {
                return ts_value_make_double(*(double*)&val);
            } else {
                return ts_value_make_int(val);
            }
        }
        return (TsValue*)val;
    }

    void* ts_array_get_unchecked(void* arr, int64_t index) {
        return (void*)((TsArray*)arr)->GetUnchecked(index);
    }

    void ts_array_set(void* arr, int64_t index, void* value) {
        ((TsArray*)arr)->Set(index, (int64_t)value);
    }

    void ts_array_set_unchecked(void* arr, int64_t index, void* value) {
        ((TsArray*)arr)->SetUnchecked(index, (int64_t)value);
    }

    int64_t ts_array_length(void* arr) {
        return ((TsArray*)arr)->Length();
    }

    void ts_array_sort(void* arr) {
        ((TsArray*)arr)->Sort();
    }

    // Thread-local comparator for use in std::sort
    static thread_local TsValue* g_current_comparator = nullptr;

    static bool comparator_wrapper(int64_t a, int64_t b) {
        if (!g_current_comparator) return a < b;
        
        // Create boxed values for a and b
        TsValue* aVal = ts_value_make_int(a);
        TsValue* bVal = ts_value_make_int(b);
        
        // Call the comparator function using ts_call_2
        TsValue* result = ts_call_2(g_current_comparator, aVal, bVal);
        if (!result) return a < b;
        
        // Get the result as an int (negative = a < b, zero = equal, positive = a > b)
        int64_t cmp = ts_value_get_int(result);
        return cmp < 0;
    }

    void ts_array_sort_with_comparator(void* arr, void* comparator) {
        if (!comparator) {
            ((TsArray*)arr)->Sort();
            return;
        }
        
        TsArray* array = (TsArray*)arr;
        g_current_comparator = (TsValue*)comparator;
        
        int64_t* elements = (int64_t*)array->GetElementsPtr();
        size_t len = array->Length();
        std::sort(elements, elements + len, comparator_wrapper);
        
        g_current_comparator = nullptr;
    }

    void* ts_array_slice(void* arr, int64_t start, int64_t end) {
        std::cout << "ts_array_slice: start=" << start << ", end=" << end << std::endl;
        return ((TsArray*)arr)->Slice(start, end);
    }

    int64_t ts_array_indexOf(void* arr, int64_t value) {
        return ((TsArray*)arr)->IndexOf(value);
    }

    void* ts_array_flat(void* arr, int64_t depth) {
        return ((TsArray*)arr)->Flat(depth);
    }

    void* ts_array_flatMap(void* arr, void* callback, void* thisArg) {
        return ((TsArray*)arr)->FlatMap(callback, thisArg);
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

    void ts_array_forEach(void* arr, void* callback, void* thisArg) {
        ((TsArray*)arr)->ForEach(callback, thisArg);
    }

    void* ts_array_map(void* arr, void* callback, void* thisArg) {
        return ((TsArray*)arr)->Map(callback, thisArg);
    }

    void* ts_array_filter(void* arr, void* callback, void* thisArg) {
        return ((TsArray*)arr)->Filter(callback, thisArg);
    }

    void* ts_array_reduce(void* arr, void* callback, void* initialValue) {
        return ((TsArray*)arr)->Reduce(callback, initialValue);
    }

    bool ts_array_some(void* arr, void* callback, void* thisArg) {
        return ((TsArray*)arr)->Some(callback, thisArg);
    }

    bool ts_array_every(void* arr, void* callback, void* thisArg) {
        return ((TsArray*)arr)->Every(callback, thisArg);
    }

    TsValue* ts_array_find(void* arr, void* callback, void* thisArg) {
        return ((TsArray*)arr)->Find(callback, thisArg);
    }

    int64_t ts_array_findIndex(void* arr, void* callback, void* thisArg) {
        return ((TsArray*)arr)->FindIndex(callback, thisArg);
    }

    void ts_array_concat(void* arr, void* other) {
        TsArray* target = (TsArray*)arr;
        TsArray* source = (TsArray*)other;
        for (size_t i = 0; i < source->Length(); ++i) {
            target->Push(source->Get(i));
        }
    }

    bool ts_array_is_array(void* value) {
        if (!value) return false;
        
        // Check if the value is a TsValue* with ARRAY_PTR type
        TsValue* v = (TsValue*)value;
        if (v->type == ValueType::ARRAY_PTR && v->ptr_val) {
            // It's a boxed array - check the underlying pointer
            uint32_t* magic_ptr = (uint32_t*)v->ptr_val;
            return *magic_ptr == TsArray::MAGIC;
        }
        
        // Check if the value has the TsArray magic number directly
        // The magic is the first field of TsArray (at offset 0)
        uint32_t* magic_ptr = (uint32_t*)value;
        return *magic_ptr == TsArray::MAGIC;
    }
}
