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
    if (!cbVal || cbVal->type != ValueType::FUNCTION_PTR) return;

    for (size_t i = 0; i < length; ++i) {
        TsValue* v = (((int64_t*)elements)[i] > 0xFFFFFFFF || ((int64_t*)elements)[i] < 0) ? ts_value_make_object((void*)((int64_t*)elements)[i]) : ts_value_make_int(((int64_t*)elements)[i]);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        ts_call_3(cbVal, v, idx, arr);
    }
}

void* TsArray::Map(void* callback, void* thisArg) {
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal || cbVal->type != ValueType::FUNCTION_PTR) return nullptr;

    TsArray* result = TsArray::Create(length);
    for (size_t i = 0; i < length; ++i) {
        TsValue* v = (((int64_t*)elements)[i] > 0xFFFFFFFF || ((int64_t*)elements)[i] < 0) ? ts_value_make_object((void*)((int64_t*)elements)[i]) : ts_value_make_int(((int64_t*)elements)[i]);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = ts_call_3(cbVal, v, idx, arr);
        
        // Always store the TsValue* pointer - let the print code handle type inspection
        result->Push((int64_t)res);
    }
    return result;
}

void* TsArray::Filter(void* callback, void* thisArg) {
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal || cbVal->type != ValueType::FUNCTION_PTR) return nullptr;

    TsArray* result = TsArray::Create();
    for (size_t i = 0; i < length; ++i) {
        TsValue* v = (((int64_t*)elements)[i] > 0xFFFFFFFF || ((int64_t*)elements)[i] < 0) ? ts_value_make_object((void*)((int64_t*)elements)[i]) : ts_value_make_int(((int64_t*)elements)[i]);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = ts_call_3(cbVal, v, idx, arr);
        
        if (res->type == ValueType::BOOLEAN && res->b_val) {
            result->Push(((int64_t*)elements)[i]);
        }
    }
    return result;
}

void* TsArray::Reduce(void* callback, void* initialValue) {
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal || cbVal->type != ValueType::FUNCTION_PTR) return nullptr;

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
        accumulator = ts_call_4(cbVal, accumulator, v, idx, arr);
    }
    return accumulator;
}

bool TsArray::Some(void* callback, void* thisArg) {
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal || cbVal->type != ValueType::FUNCTION_PTR) return false;

    for (size_t i = 0; i < length; ++i) {
        TsValue* v = (((int64_t*)elements)[i] > 0xFFFFFFFF || ((int64_t*)elements)[i] < 0) ? ts_value_make_object((void*)((int64_t*)elements)[i]) : ts_value_make_int(((int64_t*)elements)[i]);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = ts_call_3(cbVal, v, idx, arr);
        if (res->type == ValueType::BOOLEAN && res->b_val) return true;
    }
    return false;
}

bool TsArray::Every(void* callback, void* thisArg) {
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal || cbVal->type != ValueType::FUNCTION_PTR) return false;

    for (size_t i = 0; i < length; ++i) {
        TsValue* v = (((int64_t*)elements)[i] > 0xFFFFFFFF || ((int64_t*)elements)[i] < 0) ? ts_value_make_object((void*)((int64_t*)elements)[i]) : ts_value_make_int(((int64_t*)elements)[i]);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = ts_call_3(cbVal, v, idx, arr);
        if (res->type == ValueType::BOOLEAN && !res->b_val) return false;
    }
    return true;
}

TsValue* TsArray::Find(void* callback, void* thisArg) {
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal || cbVal->type != ValueType::FUNCTION_PTR) return ts_value_make_undefined();

    for (size_t i = 0; i < length; ++i) {
        TsValue* v = (((int64_t*)elements)[i] > 0xFFFFFFFF || ((int64_t*)elements)[i] < 0) ? ts_value_make_object((void*)((int64_t*)elements)[i]) : ts_value_make_int(((int64_t*)elements)[i]);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = ts_call_3(cbVal, v, idx, arr);
        if (res->type == ValueType::BOOLEAN && res->b_val) {
            return (((int64_t*)elements)[i] > 0xFFFFFFFF || ((int64_t*)elements)[i] < 0) ? ts_value_make_object((void*)((int64_t*)elements)[i]) : ts_value_make_int(((int64_t*)elements)[i]);
        }
    }
    return ts_value_make_undefined();
}

int64_t TsArray::FindIndex(void* callback, void* thisArg) {
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal || cbVal->type != ValueType::FUNCTION_PTR) return -1;

    for (size_t i = 0; i < length; ++i) {
        TsValue* v = (((int64_t*)elements)[i] > 0xFFFFFFFF || ((int64_t*)elements)[i] < 0) ? ts_value_make_object((void*)((int64_t*)elements)[i]) : ts_value_make_int(((int64_t*)elements)[i]);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = ts_call_3(cbVal, v, idx, arr);
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

    // Value-based variant - returns TsValue by value to avoid heap allocation
    TsValue ts_array_get_v(void* arr, int64_t index) {
        TsValue result;
        result.type = ValueType::UNDEFINED;
        result.ptr_val = nullptr;
        
        if (!arr) return result;
        
        TsArray* array = (TsArray*)arr;
        if (index < 0 || index >= array->Length()) {
            return result;
        }
        
        int64_t val = array->Get(index);
        if (array->IsSpecialized()) {
            if (array->IsDouble()) {
                result.type = ValueType::NUMBER_DBL;
                result.d_val = *(double*)&val;
            } else {
                result.type = ValueType::NUMBER_INT;
                result.i_val = val;
            }
            return result;
        }
        
        // For non-specialized arrays, the stored value is a TsValue*
        TsValue* stored = (TsValue*)val;
        if (stored) {
            return *stored;
        }
        return result;
    }

    // Value-based set - takes TsValue by value
    void ts_array_set_v(void* arr, int64_t index, TsValue value) {
        if (!arr) return;
        TsArray* array = (TsArray*)arr;
        
        printf("[ts_array_set_v] arr=%p index=%lld value.type=%d value.i_val=%lld\n",
               arr, index, (int)value.type, value.i_val);
        
        if (array->IsSpecialized()) {
            if (array->IsDouble()) {
                double d = (value.type == ValueType::NUMBER_DBL) ? value.d_val : (double)value.i_val;
                int64_t bits;
                memcpy(&bits, &d, sizeof(bits));
                array->Set(index, bits);
            } else {
                int64_t i = (value.type == ValueType::NUMBER_INT) ? value.i_val : (int64_t)value.d_val;
                array->Set(index, i);
            }
        } else {
            // For non-specialized, we need to allocate a TsValue
            TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
            *v = value;
            printf("[ts_array_set_v] Storing TsValue* %p at index %lld\n", v, index);
            array->Set(index, (int64_t)v);
        }
    }

    void* ts_array_get_unchecked(void* arr, int64_t index) {
        return (void*)((TsArray*)arr)->GetUnchecked(index);
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
        int64_t rawVal = ((TsArray*)arr)->At(index);
        // Box the raw value as a TsValue so unboxValue can unwrap it properly
        return (void*)ts_value_make_int(rawVal);
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
    
    // ============================================================
    // Inline IR Helpers - Scalar-based API to avoid struct passing
    // ============================================================
    
    // Get array element via out-parameters (avoids struct return)
    void __ts_array_get_inline(void* arr, int64_t index, uint8_t* out_type, int64_t* out_value) {
        if (!arr || index < 0) {
            *out_type = (uint8_t)ValueType::UNDEFINED;
            *out_value = 0;
            return;
        }
        
        TsArray* array = (TsArray*)arr;
        if (index >= array->Length()) {
            *out_type = (uint8_t)ValueType::UNDEFINED;
            *out_value = 0;
            return;
        }
        
        int64_t raw_val = array->GetUnchecked(index);
        
        // For specialized arrays, return the raw value directly
        if (array->IsSpecialized()) {
            if (array->IsDouble()) {
                *out_type = (uint8_t)ValueType::NUMBER_DBL;
            } else {
                *out_type = (uint8_t)ValueType::NUMBER_INT;
            }
            *out_value = raw_val;
            return;
        }
        
        // For non-specialized arrays, the stored value is a TsValue* pointer
        // We need to dereference it to get the actual type and value
        if (raw_val == 0) {
            *out_type = (uint8_t)ValueType::UNDEFINED;
            *out_value = 0;
            return;
        }
        
        TsValue* stored = (TsValue*)raw_val;
        *out_type = (uint8_t)stored->type;
        *out_value = stored->i_val;  // Union - works for any type
    }
    
    // Set array element from separate type/value
    // For non-specialized arrays, we need to store TsValue* pointers, not raw values
    void __ts_array_set_inline(void* arr, int64_t index, uint8_t val_type, int64_t val_value) {
        if (!arr || index < 0) return;
        
        TsArray* array = (TsArray*)arr;
        
        // For specialized arrays, store raw value
        if (array->IsSpecialized()) {
            array->Set(index, val_value);
            return;
        }
        
        // For non-specialized arrays, we need to store a TsValue*
        // Reconstruct a boxed TsValue from the type and value
        TsValue* boxed = (TsValue*)ts_alloc(sizeof(TsValue));
        boxed->type = (ValueType)val_type;
        boxed->i_val = val_value;  // Union - works for any type
        
        array->Set(index, (int64_t)boxed);
    }
    
    // Get array length (ensure it's exported for inline IR)
    int64_t __ts_array_length(void* arr) {
        if (!arr) return 0;
        return ((TsArray*)arr)->Length();
    }
}
