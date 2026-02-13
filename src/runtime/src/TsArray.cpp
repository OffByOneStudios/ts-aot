#include "TsArray.h"
#include "TsObject.h"
#include "TsNanBox.h"
#include "TsMap.h"
#include "TsRegExp.h"
#include "TsRuntime.h"
#include "TsClosure.h"
#include "TsFlatObject.h"
#include "GC.h"
#include "TsGC.h"
#include <cstring>
#include <cmath>
#include <iostream>
#include <new>
#include <algorithm>

TsArray* TsArray::Create(size_t initialCapacity) {
    void* mem = ts_alloc(sizeof(TsArray));
    return new(mem) TsArray(initialCapacity, 8);
}

void TsArray::InitInPlace(void* mem, size_t initialCapacity) {
    if (!mem) return;
    new(mem) TsArray(initialCapacity, 8);
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

// Helper to get element at index as a boxed TsValue*, handling specialized arrays
TsValue* TsArray::GetElementBoxed(size_t index) {
    if (index >= length) return ts_value_make_undefined();

    if (isSpecialized) {
        if (isDouble) {
            double val = ((double*)elements)[index];
            return ts_value_make_double(val);
        } else {
            int64_t val = ((int64_t*)elements)[index];
            return ts_value_make_int(val);
        }
    }

    int64_t val = ((int64_t*)elements)[index];
    ElementKind kind = elementKind_;

    // V8-style SMI: stored as raw int
    if (kind == ElementKind::PackedSmi || kind == ElementKind::HoleySmi) {
        return ts_value_make_int(val);
    }

    // V8-style Double: stored as raw double bits
    if (kind == ElementKind::PackedDouble || kind == ElementKind::HoleyDouble) {
        double d;
        memcpy(&d, &val, sizeof(d));
        return ts_value_make_double(d);
    }

    // PackedAny / generic: stored values are NaN-boxed uint64_t
    // Return as-is (the stored value IS the NaN-boxed representation)
    return (TsValue*)(uintptr_t)(uint64_t)val;
}

TsArray::TsArray(size_t initialCapacity, size_t elementSize) {
    this->capacity = initialCapacity > 0 ? initialCapacity : 4;
    this->length = 0;
    this->elementSize = elementSize;
    this->elements = ts_alloc(this->capacity * this->elementSize);
}

void TsArray::Push(int64_t value) {
    if (length >= capacity) {
        size_t newCapacity = capacity * 2;
        if (newCapacity < 4) newCapacity = 4;
        void* newElements = ts_alloc(newCapacity * elementSize);
        std::memcpy(newElements, elements, length * elementSize);
        elements = newElements;
        // Write barrier: elements field now points to potentially-nursery buffer
        ts_gc_write_barrier((void*)&this->elements, newElements);
        capacity = newCapacity;
    }
    if (elementSize == 8) {
        ((int64_t*)elements)[length++] = value;
        // Write barrier: value may be a pointer to a nursery object
        ts_gc_write_barrier(&((int64_t*)elements)[length-1], (void*)value);
    } else if (elementSize == 4) {
        ((int32_t*)elements)[length++] = (int32_t)value;
    } else if (elementSize == 2) {
        ((int16_t*)elements)[length++] = (int16_t)value;
    } else if (elementSize == 1) {
        ((int8_t*)elements)[length++] = (int8_t)value;
    } else {
        // Fallback: memcpy the lower bytes
        std::memcpy((char*)elements + length * elementSize, &value, elementSize);
        length++;
    }
}

int64_t TsArray::Pop() {
    if (length == 0) return 0;
    if (elementSize == 8) {
        return ((int64_t*)elements)[--length];
    } else if (elementSize == 4) {
        return (int64_t)((int32_t*)elements)[--length];
    } else if (elementSize == 2) {
        return (int64_t)((int16_t*)elements)[--length];
    } else if (elementSize == 1) {
        return (int64_t)((int8_t*)elements)[--length];
    }
    // Fallback
    int64_t result = 0;
    length--;
    std::memcpy(&result, (char*)elements + length * elementSize, elementSize);
    return result;
}

double TsArray::GetElementDouble(size_t index) {
    if (index >= length) return 0.0;

    if (isSpecialized && isDouble) {
        // Specialized double array - elements stored directly as doubles
        return ((double*)elements)[index];
    }

    if (isSpecialized) {
        // Specialized int array - raw integers
        return (double)((int64_t*)elements)[index];
    }

    // Generic array - element is a NaN-boxed value
    uint64_t nb = (uint64_t)((int64_t*)elements)[index];
    return nanbox_to_number(nb);
}

void TsArray::PushDouble(double value) {
    if (isSpecialized && isDouble) {
        // Specialized double array - store directly
        if (length >= capacity) {
            size_t newCapacity = capacity * 2;
            void* newElements = ts_alloc(newCapacity * elementSize);
            std::memcpy(newElements, elements, length * elementSize);
            elements = newElements;
            // Write barrier: elements field now points to potentially-nursery buffer
            ts_gc_write_barrier((void*)&this->elements, newElements);
            capacity = newCapacity;
        }
        ((double*)elements)[length++] = value;
    } else {
        // Generic array - box the double and store the pointer
        TsValue* boxed = ts_value_make_double(value);
        Push((int64_t)boxed);
    }
}

void TsArray::Unshift(int64_t value) {
    if (length >= capacity) {
        size_t newCapacity = capacity * 2;
        if (newCapacity < 4) newCapacity = 4;
        void* newElements = ts_alloc(newCapacity * elementSize);
        std::memcpy((char*)newElements + elementSize, elements, length * elementSize);
        elements = newElements;
        // Write barrier: elements field now points to potentially-nursery buffer
        ts_gc_write_barrier((void*)&this->elements, newElements);
        capacity = newCapacity;
    } else {
        std::memmove((char*)elements + elementSize, elements, length * elementSize);
    }
    if (elementSize == 8) {
        ((int64_t*)elements)[0] = value;
        // Write barrier: value may be a pointer to a nursery object
        ts_gc_write_barrier(&((int64_t*)elements)[0], (void*)value);
    } else {
        std::memcpy(elements, &value, elementSize);
    }
    length++;
}

int64_t TsArray::Shift() {
    if (length == 0) return 0;
    int64_t result = 0;
    if (elementSize == 8) {
        result = ((int64_t*)elements)[0];
    } else {
        std::memcpy(&result, elements, elementSize);
    }
    std::memmove(elements, (char*)elements + elementSize, (length - 1) * elementSize);
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
    // Write barrier: value may be a pointer to a nursery object
    ts_gc_write_barrier(&((int64_t*)elements)[index], (void*)value);
}

int64_t TsArray::Length() {
    return length;
}

// JavaScript default sort: convert NaN-boxed elements to strings and compare lexicographically.
// undefined values sort to the end.
static const char* elementToSortString(int64_t elem, char* buf, size_t bufSize) {
    uint64_t nb = (uint64_t)elem;

    if (nanbox_is_undefined(nb) || nb == 0) return "undefined";
    if (nanbox_is_null(nb)) return "null";
    if (nanbox_is_bool(nb)) return nanbox_to_bool(nb) ? "true" : "false";
    if (nanbox_is_int32(nb)) {
        snprintf(buf, bufSize, "%d", nanbox_to_int32(nb));
        return buf;
    }
    if (nanbox_is_double(nb)) {
        snprintf(buf, bufSize, "%.17g", nanbox_to_double(nb));
        // Trim trailing zeros for cleaner output (match JS behavior)
        if (strchr(buf, '.')) {
            size_t len = strlen(buf);
            while (len > 1 && buf[len-1] == '0') buf[--len] = '\0';
            if (len > 0 && buf[len-1] == '.') buf[--len] = '\0';
        }
        return buf;
    }
    if (nanbox_is_ptr(nb)) {
        void* ptr = nanbox_to_ptr(nb);
        if (!ptr) return "null";
        uint32_t magic = *(uint32_t*)ptr;
        if (magic == TsString::MAGIC) {
            return ((TsString*)ptr)->ToUtf8();
        }
        return "[object Object]";
    }

    // Fallback
    return "[object Object]";
}

static bool jsDefaultSortComparator(int64_t a, int64_t b) {
    // undefined sorts to the end
    uint64_t nba = (uint64_t)a;
    uint64_t nbb = (uint64_t)b;
    bool aUndef = nanbox_is_undefined(nba) || nba == 0;
    bool bUndef = nanbox_is_undefined(nbb) || nbb == 0;
    if (aUndef && bUndef) return false;
    if (aUndef) return false;  // a goes after b
    if (bUndef) return true;   // a goes before b

    char bufA[64], bufB[64];
    const char* sa = elementToSortString(a, bufA, sizeof(bufA));
    const char* sb = elementToSortString(b, bufB, sizeof(bufB));
    return strcmp(sa, sb) < 0;
}

void TsArray::Sort() {
    std::sort((int64_t*)elements, ((int64_t*)elements) + length, jsDefaultSortComparator);
}

int64_t TsArray::IndexOf(int64_t value) {
    // For PackedDouble arrays, codegen passes raw double bits directly (via bitcast).
    // Handle this FIRST to avoid trying to dereference double bits as a pointer.
    if (isSpecialized && isDouble) {
        // Value is raw IEEE 754 double bits - compare directly
        double searchVal;
        memcpy(&searchVal, &value, sizeof(double));

        for (size_t i = 0; i < length; ++i) {
            if (((double*)elements)[i] == searchVal) return (int64_t)i;
        }
        return -1;
    }

    // For specialized integer arrays (PackedSmi), unbox the NaN-boxed search value
    if (isSpecialized) {
        uint64_t nb = (uint64_t)value;
        int64_t rawValue;
        if (nanbox_is_int32(nb)) {
            rawValue = (int64_t)nanbox_to_int32(nb);
        } else if (nanbox_is_double(nb)) {
            rawValue = (int64_t)nanbox_to_double(nb);
        } else {
            rawValue = value;  // raw int passed directly
        }
        for (size_t i = 0; i < length; ++i) {
            if (((int64_t*)elements)[i] == rawValue) return (int64_t)i;
        }
        return -1;
    }

    // For non-specialized (generic) arrays, elements are NaN-boxed values stored as int64.
    // The search value is also NaN-boxed. Compare using NaN-box decoding.
    uint64_t searchNB = (uint64_t)value;

    for (size_t i = 0; i < length; ++i) {
        uint64_t elemNB = (uint64_t)((int64_t*)elements)[i];

        // Fast path: exact bit-level match (handles ints, bools, null, undefined, same pointer)
        if (elemNB == searchNB) return (int64_t)i;

        // String comparison: both must be pointers to TsString objects
        if (nanbox_is_ptr(elemNB) && nanbox_is_ptr(searchNB)) {
            void* ep = nanbox_to_ptr(elemNB);
            void* sp = nanbox_to_ptr(searchNB);
            if (ep && sp) {
                uint32_t eMagic = *(uint32_t*)ep;
                uint32_t sMagic = *(uint32_t*)sp;
                if (eMagic == TsString::MAGIC && sMagic == TsString::MAGIC) {
                    if (((TsString*)ep)->Equals((TsString*)sp)) return (int64_t)i;
                }
            }
        }

        // Numeric cross-type: int32 vs double with same numeric value
        if (nanbox_is_number(elemNB) && nanbox_is_number(searchNB)) {
            double ed = nanbox_to_number(elemNB);
            double sd = nanbox_to_number(searchNB);
            if (ed == sd) return (int64_t)i;
        }
    }
    return -1;
}

int64_t TsArray::LastIndexOf(int64_t value) {
    // For PackedDouble arrays, codegen passes raw double bits directly (via bitcast).
    // Handle this FIRST to avoid trying to dereference double bits as a pointer.
    if (isSpecialized && isDouble) {
        // Value is raw IEEE 754 double bits - compare directly
        double searchVal;
        memcpy(&searchVal, &value, sizeof(double));

        for (size_t i = length; i > 0; --i) {
            if (((double*)elements)[i - 1] == searchVal) return (int64_t)(i - 1);
        }
        return -1;
    }

    // For specialized integer arrays (PackedSmi), unbox the search value and compare directly
    if (isSpecialized) {
        int64_t rawValue = value;
        // Decode NaN-boxed value to extract integer for comparison
        TsValue decoded = nanbox_to_tagged((TsValue*)value);
        if (decoded.type == ValueType::NUMBER_INT) {
            rawValue = decoded.i_val;
        } else if (decoded.type == ValueType::NUMBER_DBL) {
            rawValue = (int64_t)decoded.d_val;
        }
        for (size_t i = length; i > 0; --i) {
            if (((int64_t*)elements)[i - 1] == rawValue) return (int64_t)(i - 1);
        }
        return -1;
    }

    // For non-specialized (generic) arrays, elements are TsValue* pointers stored as int64.
    // The search value is also a TsValue* - we need to compare values, not addresses.
    // For non-specialized (generic) arrays, use NaN-box comparison
    uint64_t searchNB = (uint64_t)value;

    for (size_t i = length; i > 0; --i) {
        uint64_t elemNB = (uint64_t)((int64_t*)elements)[i - 1];

        // Fast path: exact bit-level match
        if (elemNB == searchNB) return (int64_t)(i - 1);

        // String comparison
        if (nanbox_is_ptr(elemNB) && nanbox_is_ptr(searchNB)) {
            void* ep = nanbox_to_ptr(elemNB);
            void* sp = nanbox_to_ptr(searchNB);
            if (ep && sp) {
                uint32_t eMagic = *(uint32_t*)ep;
                uint32_t sMagic = *(uint32_t*)sp;
                if (eMagic == TsString::MAGIC && sMagic == TsString::MAGIC) {
                    if (((TsString*)ep)->Equals((TsString*)sp)) return (int64_t)(i - 1);
                }
            }
        }

        // Numeric cross-type comparison
        if (nanbox_is_number(elemNB) && nanbox_is_number(searchNB)) {
            double ed = nanbox_to_number(elemNB);
            double sd = nanbox_to_number(searchNB);
            if (ed == sd) return (int64_t)(i - 1);
        }
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
        uint64_t nb = (uint64_t)val;
        if (depth > 0 && nanbox_is_ptr(nb)) {
            void* ptr = nanbox_to_ptr(nb);
            if (ptr) {
                // Check magic at offset 0 for TsArray
                uint32_t magic = *(uint32_t*)ptr;
                if (magic == TsArray::MAGIC) {
                    TsArray* sub = (TsArray*)ptr;
                    TsArray* flattenedSub = (TsArray*)sub->Flat(depth - 1);
                    for (size_t j = 0; j < flattenedSub->length; ++j) {
                        result->Push(((int64_t*)flattenedSub->elements)[j]);
                    }
                    continue;
                }
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
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback)) {
        TsClosure* closure = (TsClosure*)callback;
        for (size_t i = 0; i < length; ++i) {
            // Get element as double (unboxed) for HIR closures
            double elem = GetElementDouble(i);
            ts_closure_invoke_1d_void(closure, elem);
        }
        return;
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return;

    for (size_t i = 0; i < length; ++i) {
        TsValue* v = GetElementBoxed(i);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        ts_call_3(cbVal, v, idx, arr);
    }
}

void* TsArray::Map(void* callback, void* thisArg) {
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback)) {
        TsClosure* closure = (TsClosure*)callback;
        TsArray* result = TsArray::Create(length);
        for (size_t i = 0; i < length; ++i) {
            // Get element as double (unboxed) for HIR closures
            double elem = GetElementDouble(i);
            double mapped = ts_closure_invoke_1d(closure, elem);
            // Store as double directly (using bit pattern)
            result->PushDouble(mapped);
        }
        return result;
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return nullptr;

    TsArray* result = TsArray::Create(length);
    for (size_t i = 0; i < length; ++i) {
        TsValue* v = GetElementBoxed(i);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = ts_call_3(cbVal, v, idx, arr);

        // Always store the TsValue* pointer - let the print code handle type inspection
        result->Push((int64_t)res);
    }
    return result;
}

void* TsArray::Filter(void* callback, void* thisArg) {
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback)) {
        TsClosure* closure = (TsClosure*)callback;
        TsArray* result = TsArray::Create();
        for (size_t i = 0; i < length; ++i) {
            double elem = GetElementDouble(i);
            if (ts_closure_invoke_1d_bool(closure, elem)) {
                result->PushDouble(elem);
            }
        }
        return result;
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return nullptr;

    // Preserve the source array's specialized type in the result
    TsArray* result;
    if (isSpecialized) {
        result = TsArray::CreateSpecialized(0, elementSize, isDouble);
    } else {
        result = TsArray::Create();
    }

    for (size_t i = 0; i < length; ++i) {
        TsValue* v = GetElementBoxed(i);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        // Use ts_call_with_arity to respect callback's declared parameter count
        TsValue* res = ts_call_with_arity(cbVal, v, idx, arr);

        // Use JavaScript truthiness, not strict boolean check
        if (ts_value_to_bool(res)) {
            // Push the raw element value - use appropriate method based on array type
            if (isSpecialized && isDouble) {
                result->PushDouble(((double*)elements)[i]);
            } else {
                result->Push(((int64_t*)elements)[i]);
            }
        }
    }
    return result;
}

void* TsArray::Reduce(void* callback, void* initialValue) {
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback)) {
        TsClosure* closure = (TsClosure*)callback;
        TsValue* accumulator = (TsValue*)initialValue;
        size_t startIdx = 0;
        if (!accumulator && length > 0) {
            accumulator = GetElementBoxed(0);
            startIdx = 1;
        }
        for (size_t i = startIdx; i < length; ++i) {
            TsValue* v = GetElementBoxed(i);
            // HIR reduce callback takes (acc, val), not (acc, val, idx, arr)
            accumulator = ts_closure_invoke_2v(closure, accumulator, v);
        }
        return accumulator;
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return nullptr;

    TsValue* accumulator = (TsValue*)initialValue;
    size_t startIdx = 0;
    if (!accumulator && length > 0) {
        accumulator = GetElementBoxed(0);
        startIdx = 1;
    }

    for (size_t i = startIdx; i < length; ++i) {
        TsValue* v = GetElementBoxed(i);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        accumulator = ts_call_4(cbVal, accumulator, v, idx, arr);
    }
    return accumulator;
}

void* TsArray::ReduceRight(void* callback, void* initialValue) {
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback)) {
        TsClosure* closure = (TsClosure*)callback;
        TsValue* accumulator = (TsValue*)initialValue;
        size_t startIdx = length;
        if (!accumulator && length > 0) {
            accumulator = GetElementBoxed(length - 1);
            startIdx = length - 1;
        }
        for (size_t i = startIdx; i > 0; --i) {
            TsValue* v = GetElementBoxed(i - 1);
            // HIR reduce callback takes (acc, val), not (acc, val, idx, arr)
            accumulator = ts_closure_invoke_2v(closure, accumulator, v);
        }
        return accumulator;
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return nullptr;

    TsValue* accumulator = (TsValue*)initialValue;
    size_t startIdx = length;
    if (!accumulator && length > 0) {
        accumulator = GetElementBoxed(length - 1);
        startIdx = length - 1;
    }

    for (size_t i = startIdx; i > 0; --i) {
        TsValue* v = GetElementBoxed(i - 1);
        TsValue* idx = ts_value_make_int(i - 1);
        TsValue* arr = ts_value_make_object(this);
        accumulator = ts_call_4(cbVal, accumulator, v, idx, arr);
    }
    return accumulator;
}

bool TsArray::Some(void* callback, void* thisArg) {
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback)) {
        TsClosure* closure = (TsClosure*)callback;
        for (size_t i = 0; i < length; ++i) {
            double elem = GetElementDouble(i);
            if (ts_closure_invoke_1d_bool(closure, elem)) return true;
        }
        return false;
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return false;

    for (size_t i = 0; i < length; ++i) {
        TsValue* v = GetElementBoxed(i);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = ts_call_3(cbVal, v, idx, arr);
        if (ts_value_to_bool(res)) return true;
    }
    return false;
}

bool TsArray::Every(void* callback, void* thisArg) {
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback)) {
        TsClosure* closure = (TsClosure*)callback;
        for (size_t i = 0; i < length; ++i) {
            double elem = GetElementDouble(i);
            if (!ts_closure_invoke_1d_bool(closure, elem)) return false;
        }
        return true;
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return false;

    for (size_t i = 0; i < length; ++i) {
        TsValue* v = GetElementBoxed(i);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = ts_call_3(cbVal, v, idx, arr);
        if (!ts_value_to_bool(res)) return false;
    }
    return true;
}

TsValue* TsArray::Find(void* callback, void* thisArg) {
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback)) {
        TsClosure* closure = (TsClosure*)callback;
        for (size_t i = 0; i < length; ++i) {
            double elem = GetElementDouble(i);
            if (ts_closure_invoke_1d_bool(closure, elem)) {
                // Return the element as a boxed double
                return ts_value_make_double(elem);
            }
        }
        return ts_value_make_undefined();
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return ts_value_make_undefined();

    for (size_t i = 0; i < length; ++i) {
        TsValue* v = GetElementBoxed(i);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = ts_call_3(cbVal, v, idx, arr);
        if (ts_value_to_bool(res)) {
            return GetElementBoxed(i);
        }
    }
    return ts_value_make_undefined();
}

int64_t TsArray::FindIndex(void* callback, void* thisArg) {
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback)) {
        TsClosure* closure = (TsClosure*)callback;
        for (size_t i = 0; i < length; ++i) {
            double elem = GetElementDouble(i);
            if (ts_closure_invoke_1d_bool(closure, elem)) return (int64_t)i;
        }
        return -1;
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return -1;

    for (size_t i = 0; i < length; ++i) {
        TsValue* v = GetElementBoxed(i);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = ts_call_3(cbVal, v, idx, arr);
        if (ts_value_to_bool(res)) return (int64_t)i;
    }
    return -1;
}

TsValue* TsArray::FindLast(void* callback, void* thisArg) {
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback)) {
        TsClosure* closure = (TsClosure*)callback;
        for (size_t i = length; i > 0; --i) {
            size_t idx = i - 1;
            double elem = GetElementDouble(idx);
            if (ts_closure_invoke_1d_bool(closure, elem)) {
                // Return the element as a boxed double
                return ts_value_make_double(elem);
            }
        }
        return ts_value_make_undefined();
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return ts_value_make_undefined();

    for (size_t i = length; i > 0; --i) {
        size_t idx = i - 1;
        TsValue* v = GetElementBoxed(idx);
        TsValue* idxVal = ts_value_make_int(idx);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = ts_call_3(cbVal, v, idxVal, arr);
        if (ts_value_to_bool(res)) {
            return GetElementBoxed(idx);
        }
    }
    return ts_value_make_undefined();
}

int64_t TsArray::FindLastIndex(void* callback, void* thisArg) {
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback)) {
        TsClosure* closure = (TsClosure*)callback;
        for (size_t i = length; i > 0; --i) {
            size_t idx = i - 1;
            double elem = GetElementDouble(idx);
            if (ts_closure_invoke_1d_bool(closure, elem)) return (int64_t)idx;
        }
        return -1;
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return -1;

    for (size_t i = length; i > 0; --i) {
        size_t idx = i - 1;
        TsValue* v = GetElementBoxed(idx);
        TsValue* idxVal = ts_value_make_int(idx);
        TsValue* arr = ts_value_make_object(this);
        TsValue* res = ts_call_3(cbVal, v, idxVal, arr);
        if (ts_value_to_bool(res)) return (int64_t)idx;
    }
    return -1;
}

int64_t TsArray::At(int64_t index) {
    if (index < 0) index = length + index;
    if (index < 0 || index >= (int64_t)length) return 0;
    return ((int64_t*)elements)[index];
}

void TsArray::Reverse() {
    if (length <= 1) return;
    int64_t* elems = (int64_t*)elements;
    for (size_t i = 0; i < length / 2; ++i) {
        size_t j = length - 1 - i;
        int64_t temp = elems[i];
        elems[i] = elems[j];
        elems[j] = temp;
    }
}

// ES2023 "change array by copy" methods
TsArray* TsArray::ToReversed() {
    TsArray* result;
    if (isSpecialized) {
        result = TsArray::CreateSpecialized(length, elementSize, isDouble);
        if (isDouble) {
            double* srcElems = (double*)elements;
            double* dstElems = (double*)result->elements;
            for (size_t i = 0; i < length; ++i) {
                dstElems[i] = srcElems[length - 1 - i];
            }
        } else {
            int64_t* srcElems = (int64_t*)elements;
            int64_t* dstElems = (int64_t*)result->elements;
            for (size_t i = 0; i < length; ++i) {
                dstElems[i] = srcElems[length - 1 - i];
            }
        }
    } else {
        result = TsArray::Create(length);
        int64_t* srcElems = (int64_t*)elements;
        for (size_t i = 0; i < length; ++i) {
            result->Push(srcElems[length - 1 - i]);
        }
    }
    return result;
}

TsArray* TsArray::ToSorted() {
    TsArray* result;
    if (isSpecialized) {
        result = TsArray::CreateSpecialized(length, elementSize, isDouble);
        if (isDouble) {
            double* srcElems = (double*)elements;
            double* dstElems = (double*)result->elements;
            for (size_t i = 0; i < length; ++i) {
                dstElems[i] = srcElems[i];
            }
        } else {
            int64_t* srcElems = (int64_t*)elements;
            int64_t* dstElems = (int64_t*)result->elements;
            for (size_t i = 0; i < length; ++i) {
                dstElems[i] = srcElems[i];
            }
        }
    } else {
        result = TsArray::Create(length);
        int64_t* srcElems = (int64_t*)elements;
        for (size_t i = 0; i < length; ++i) {
            result->Push(srcElems[i]);
        }
    }
    result->Sort();
    return result;
}

TsArray* TsArray::ToSpliced(int64_t start, int64_t deleteCount, void* items, int64_t itemCount) {
    // Normalize start
    if (start < 0) start = length + start;
    if (start < 0) start = 0;
    if (start > (int64_t)length) start = length;

    // Normalize deleteCount
    if (deleteCount < 0) deleteCount = 0;
    if (start + deleteCount > (int64_t)length) deleteCount = length - start;

    // Guard: items must be a valid heap pointer (not a raw integer from incorrect codegen)
    if (items && (uint64_t)(uintptr_t)items < 0x10000) {
        items = nullptr;
        itemCount = 0;
    }

    // Calculate new length
    size_t newLength = length - deleteCount + itemCount;

    // items is a TsArray* containing the items to insert
    TsArray* itemsArr = items ? (TsArray*)items : nullptr;

    TsArray* result;
    if (isSpecialized) {
        result = TsArray::CreateSpecialized(newLength, elementSize, isDouble);
        size_t dstIdx = 0;

        if (isDouble) {
            double* srcElems = (double*)elements;
            double* dstElems = (double*)result->elements;

            // Copy elements before start
            for (int64_t i = 0; i < start; ++i) {
                dstElems[dstIdx++] = srcElems[i];
            }
            // Insert new items - items from generic array need conversion
            for (int64_t i = 0; i < itemCount && itemsArr; ++i) {
                // Get item as int64 (stored as pointer in generic array)
                int64_t* itemElems = (int64_t*)itemsArr->GetElementsPtr();
                int64_t rawVal = itemElems[i];
                // Convert to double (rawVal is the bit pattern of the double)
                double val = *reinterpret_cast<double*>(&rawVal);
                dstElems[dstIdx++] = val;
            }
            // Copy elements after deleted section
            for (size_t i = start + deleteCount; i < length; ++i) {
                dstElems[dstIdx++] = srcElems[i];
            }
        } else {
            int64_t* srcElems = (int64_t*)elements;
            int64_t* dstElems = (int64_t*)result->elements;

            // Copy elements before start
            for (int64_t i = 0; i < start; ++i) {
                dstElems[dstIdx++] = srcElems[i];
            }
            // Insert new items
            for (int64_t i = 0; i < itemCount && itemsArr; ++i) {
                int64_t* itemElems = (int64_t*)itemsArr->GetElementsPtr();
                dstElems[dstIdx++] = itemElems[i];
            }
            // Copy elements after deleted section
            for (size_t i = start + deleteCount; i < length; ++i) {
                dstElems[dstIdx++] = srcElems[i];
            }
        }
    } else {
        result = TsArray::Create(newLength);
        int64_t* srcElems = (int64_t*)elements;

        // Copy elements before start
        for (int64_t i = 0; i < start; ++i) {
            result->Push(srcElems[i]);
        }
        // Insert new items
        for (int64_t i = 0; i < itemCount && itemsArr; ++i) {
            int64_t* itemElems = (int64_t*)itemsArr->GetElementsPtr();
            result->Push(itemElems[i]);
        }
        // Copy elements after deleted section
        for (size_t i = start + deleteCount; i < length; ++i) {
            result->Push(srcElems[i]);
        }
    }

    return result;
}

TsArray* TsArray::With(int64_t index, int64_t value) {
    // Normalize index (negative indices count from end)
    if (index < 0) index = length + index;

    // Out of bounds throws RangeError in JS, but we'll return a copy unchanged
    if (index < 0 || index >= (int64_t)length) {
        TsArray* result;
        if (isSpecialized) {
            result = TsArray::CreateSpecialized(length, elementSize, isDouble);
            std::memcpy(result->elements, elements, length * elementSize);
        } else {
            result = TsArray::Create(length);
            int64_t* srcElems = (int64_t*)elements;
            for (size_t i = 0; i < length; ++i) {
                result->Push(srcElems[i]);
            }
        }
        return result;
    }

    TsArray* result;
    if (isSpecialized) {
        result = TsArray::CreateSpecialized(length, elementSize, isDouble);
        // Copy all elements first
        std::memcpy(result->elements, elements, length * elementSize);
        // Then replace the element at index
        if (isDouble) {
            double* dstElems = (double*)result->elements;
            // value is passed as int64_t but represents a double bit pattern for specialized double arrays
            dstElems[index] = *reinterpret_cast<double*>(&value);
        } else {
            int64_t* dstElems = (int64_t*)result->elements;
            dstElems[index] = value;
        }
    } else {
        result = TsArray::Create(length);
        int64_t* srcElems = (int64_t*)elements;
        for (size_t i = 0; i < length; ++i) {
            if ((int64_t)i == index) {
                result->Push(value);
            } else {
                result->Push(srcElems[i]);
            }
        }
    }
    return result;
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

    // Preserve specialized array type and element kind
    if (isSpecialized) {
        result->isSpecialized = true;
        result->isDouble = isDouble;
    }
    result->elementKind_ = elementKind_;

    for (size_t i = 0; i < newLength; ++i) {
        int64_t rawBits = ((int64_t*)elements)[start + i];
        result->Push(rawBits);
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

        // Handle specialized arrays - output numeric values directly
        if (isSpecialized) {
            if (isDouble) {
                double val = ((double*)elements)[i];
                // Format to avoid trailing zeros (like JavaScript)
                if (val == (int64_t)val) {
                    ss << (int64_t)val;  // Whole number, print as int
                } else {
                    ss << val;
                }
            } else {
                ss << ((int64_t*)elements)[i];
            }
            continue;
        }

        // Generic array - elements are NaN-boxed values
        uint64_t nb = (uint64_t)((int64_t*)elements)[i];
        if (nanbox_is_undefined(nb)) {
            // undefined joins as empty string (JS spec)
        } else if (nanbox_is_null(nb) || nb == 0) {
            // null joins as empty string (JS spec)
        } else if (nanbox_is_int32(nb)) {
            ss << nanbox_to_int32(nb);
        } else if (nanbox_is_double(nb)) {
            double d = nanbox_to_double(nb);
            if (d == (int64_t)d && !std::isinf(d)) {
                ss << (int64_t)d;
            } else {
                ss << d;
            }
        } else if (nanbox_is_bool(nb)) {
            ss << (nanbox_to_bool(nb) ? "true" : "false");
        } else if (nanbox_is_ptr(nb)) {
            void* ptr = nanbox_to_ptr(nb);
            if (!ptr) {
                // null pointer
            } else {
                uint32_t magic = *(uint32_t*)ptr;
                if (magic == TsString::MAGIC) {
                    ss << ((TsString*)ptr)->ToUtf8();
                } else if (magic == TsArray::MAGIC) {
                    // Nested array: recursively join with comma
                    TsString* sub = (TsString*)((TsArray*)ptr)->Join(nullptr);
                    if (sub) ss << sub->ToUtf8();
                } else {
                    // Other object - use ts_string_from_value
                    TsString* str = (TsString*)ts_string_from_value((TsValue*)(uintptr_t)nb);
                    if (str) ss << str->ToUtf8();
                    else ss << "[object Object]";
                }
            }
        } else {
            // Fallback: use ts_string_from_value
            TsString* str = (TsString*)ts_string_from_value((TsValue*)(uintptr_t)nb);
            if (str) ss << str->ToUtf8();
        }
    }
    return TsString::Create(ss.str().c_str());
}

// ============================================================
// Element Kind Transitions (V8-style)
// ============================================================

void TsArray::TransitionTo(ElementKind newKind) {
    // Don't transition if already at or beyond the target kind
    if (elementKind_ == newKind) return;

    // Transitions are one-way: more specific -> more general
    // PackedSmi -> PackedDouble -> PackedAny
    // HoleySmi -> HoleyDouble -> HoleyAny

    // Get the new element kind by meeting the current and target
    ElementKind oldKind = elementKind_;

    // Determine if we need to convert element storage
    bool wasDouble = (oldKind == ElementKind::PackedDouble || oldKind == ElementKind::HoleyDouble || isDouble);
    bool willBeDouble = (newKind == ElementKind::PackedDouble || newKind == ElementKind::HoleyDouble);

    // Handle SMI -> Double transition: convert int64 to double bits
    if ((oldKind == ElementKind::PackedSmi || oldKind == ElementKind::HoleySmi) && willBeDouble) {
        // Convert all SMI values to double
        for (size_t i = 0; i < length; ++i) {
            int64_t smiVal = ((int64_t*)elements)[i];
            double dblVal = (double)smiVal;
            int64_t bits;
            memcpy(&bits, &dblVal, sizeof(bits));
            ((int64_t*)elements)[i] = bits;
        }
        isDouble = true;
    }

    // Handle Double -> Any transition: box all values
    if (wasDouble && newKind == ElementKind::PackedAny) {
        // Need to box all double values
        for (size_t i = 0; i < length; ++i) {
            int64_t bits = ((int64_t*)elements)[i];
            double dblVal;
            memcpy(&dblVal, &bits, sizeof(dblVal));
            TsValue* boxed = ts_value_make_double(dblVal);
            ((int64_t*)elements)[i] = (int64_t)boxed;
            // Write barrier: boxed value may be in nursery
            ts_gc_write_barrier(&((int64_t*)elements)[i], (void*)boxed);
        }
        isDouble = false;
        isSpecialized = false;
    }

    // Handle SMI -> Any transition: box all values
    if ((oldKind == ElementKind::PackedSmi || oldKind == ElementKind::HoleySmi) &&
        (newKind == ElementKind::PackedAny || newKind == ElementKind::HoleyAny)) {
        // Box all SMI values
        for (size_t i = 0; i < length; ++i) {
            int64_t smiVal = ((int64_t*)elements)[i];
            TsValue* boxed = ts_value_make_int(smiVal);
            ((int64_t*)elements)[i] = (int64_t)boxed;
            // Write barrier: boxed value may be in nursery
            ts_gc_write_barrier(&((int64_t*)elements)[i], (void*)boxed);
        }
        isSpecialized = false;
    }

    elementKind_ = newKind;
}

extern "C" {
    void* ts_array_create() {
        return TsArray::Create();
    }

    void* ts_array_create_sized(int64_t size) {
        return TsArray::CreateSized((size_t)size);
    }

    void ts_array_init_inplace(void* mem, int64_t initial_capacity) {
        TsArray::InitInPlace(mem, initial_capacity > 0 ? (size_t)initial_capacity : 8);
    }

    void* ts_array_create_specialized(int64_t size, int64_t elementSize, bool isDouble) {
        return TsArray::CreateSpecialized((size_t)size, (size_t)elementSize, isDouble);
    }

    void* ts_array_get_elements_ptr(void* arr) {
        return ((TsArray*)arr)->GetElementsPtr();
    }

    bool ts_array_is_specialized(void* arr) {
        if (!arr) return false;
        // Check magic - TsRegExpMatchArray is never specialized
        uint32_t magic = *(uint32_t*)arr;
        if (magic == 0x524D4154) { // TsRegExpMatchArray::MAGIC ("RMAT")
            return false;
        }
        return ((TsArray*)arr)->IsSpecialized();
    }

    void ts_array_push(void* arr, void* value) {
        // Unbox arr if it's a NaN-boxed TsValue* pointing to an array
        void* rawArr = arr;
        if (arr) {
            TsValue decoded = nanbox_to_tagged((TsValue*)arr);
            if ((decoded.type == ValueType::OBJECT_PTR || decoded.type == ValueType::ARRAY_PTR) && decoded.ptr_val) {
                rawArr = decoded.ptr_val;
            }
        }

        if (!rawArr) {
            std::cerr << "ts_array_push: null array pointer" << std::endl;
            return;
        }

        TsArray* array = (TsArray*)rawArr;
        int64_t bits = (int64_t)value;
        ElementKind kind = array->GetElementKind();

        // ==== FAST PATH: Specialized arrays (values passed as raw bits via inttoptr) ====
        // When arrays are specialized (IsSpecialized() or IsDouble()), the codegen passes
        // values directly as i64 bits (via inttoptr), NOT as TsValue* pointers.
        // We must NOT try to dereference these values!

        // For specialized double arrays, handle the value appropriately
        if (array->IsDouble()) {
            // The value arrives as i64 bits via inttoptr. It could be:
            // 1. Raw double bits (from bitcast double -> i64 -> inttoptr)
            // 2. Raw integer value (from inttoptr i64 directly)
            //
            // Heuristic: If interpreting the bits as a double gives a tiny
            // denormal value (< 1e-100), it's probably an integer that needs
            // conversion. Real doubles rarely have such small magnitudes.
            double asDouble;
            memcpy(&asDouble, &bits, sizeof(asDouble));

            // Check for denormal/tiny values that are likely integers
            // Also check for zero (which is a valid double but also int 0)
            double absMag = asDouble < 0 ? -asDouble : asDouble;
            if (absMag < 1e-100 && bits != 0) {
                // Likely an integer - convert to double
                double dval = (double)bits;
                memcpy(&bits, &dval, sizeof(bits));
            }
            // Otherwise, bits are already valid double bits - use as-is
            array->Push(bits);
            return;
        }

        // For specialized integer arrays, store raw value directly
        if (array->IsSpecialized()) {
            // Value is passed as raw i64 bits via inttoptr - just store it
            array->Push(bits);
            return;
        }

        // ==== SLOW PATH: Non-specialized arrays with NaN-boxed values ====
        // With NaN boxing, `value` is a NaN-boxed uint64_t (not a pointer to a TsValue struct).
        // Decode using nanbox_is_* / nanbox_to_* helpers.
        uint64_t nb = (uint64_t)(uintptr_t)value;

        // PackedSmi: Try to store as unboxed int64
        if (kind == ElementKind::PackedSmi || kind == ElementKind::HoleySmi) {
            if (nanbox_is_int32(nb)) {
                int32_t val = nanbox_to_int32(nb);
                if (val >= -1073741824 && val <= 1073741823) {
                    array->Push((int64_t)val);
                    return;
                }
                array->TransitionTo(ElementKind::PackedDouble);
                double dval = (double)val;
                int64_t dblBits;
                memcpy(&dblBits, &dval, sizeof(dblBits));
                array->Push(dblBits);
                return;
            } else if (nanbox_is_double(nb)) {
                array->TransitionTo(ElementKind::PackedDouble);
                double dval = nanbox_to_double(nb);
                int64_t dblBits;
                memcpy(&dblBits, &dval, sizeof(dblBits));
                array->Push(dblBits);
                return;
            } else {
                // Non-numeric (pointer, bool, null, undefined) - transition to PackedAny
                array->TransitionTo(ElementKind::PackedAny);
                array->Push((int64_t)nb);  // Store NaN-boxed value directly
                return;
            }
        }

        // PackedDouble: Store as unboxed double bits
        if (kind == ElementKind::PackedDouble || kind == ElementKind::HoleyDouble) {
            if (nanbox_is_double(nb)) {
                double dval = nanbox_to_double(nb);
                int64_t dblBits;
                memcpy(&dblBits, &dval, sizeof(dblBits));
                array->Push(dblBits);
                return;
            } else if (nanbox_is_int32(nb)) {
                double dval = (double)nanbox_to_int32(nb);
                int64_t dblBits;
                memcpy(&dblBits, &dval, sizeof(dblBits));
                array->Push(dblBits);
                return;
            } else {
                // Non-numeric - transition to PackedAny
                array->TransitionTo(ElementKind::PackedAny);
                array->Push((int64_t)nb);  // Store NaN-boxed value directly
                return;
            }
        }

        // ==== PackedAny / generic path ====
        // Store NaN-boxed value directly (no heap allocation needed)
        array->Push((int64_t)nb);
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
            return ts_value_make_undefined();
        }
        // Check magic to handle TsRegExpMatchArray
        uint32_t magic = *(uint32_t*)arr;
        if (magic == 0x524D4154) { // TsRegExpMatchArray::MAGIC ("RMAT")
            TsRegExpMatchArray* match = (TsRegExpMatchArray*)arr;
            if (index < 0 || index >= match->Length()) {
                return ts_value_make_undefined();
            }
            return (TsValue*)match->Get((size_t)index);
        }
        TsArray* array = (TsArray*)arr;
        if (index < 0 || index >= array->Length()) {
             return ts_value_make_undefined();
        }
        int64_t val = array->Get(index);
        ElementKind kind = array->GetElementKind();

        // ==== V8-style element kind fast paths ====

        // PackedSmi: Return as boxed int
        if (kind == ElementKind::PackedSmi || kind == ElementKind::HoleySmi) {
            return ts_value_make_int(val);
        }

        // PackedDouble: Return as boxed double
        if (kind == ElementKind::PackedDouble || kind == ElementKind::HoleyDouble) {
            return ts_value_make_double(*(double*)&val);
        }

        // ==== Legacy specialized arrays (backward compat) ====
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
        ElementKind kind = array->GetElementKind();

        // ==== V8-style element kind fast paths ====

        // PackedSmi: Return as NUMBER_INT
        if (kind == ElementKind::PackedSmi || kind == ElementKind::HoleySmi) {
            result.type = ValueType::NUMBER_INT;
            result.i_val = val;
            return result;
        }

        // PackedDouble: Return as NUMBER_DBL
        if (kind == ElementKind::PackedDouble || kind == ElementKind::HoleyDouble) {
            result.type = ValueType::NUMBER_DBL;
            result.d_val = *(double*)&val;
            return result;
        }

        // ==== Legacy specialized arrays (backward compat) ====
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

        // For non-specialized arrays, the stored value is a NaN-boxed uint64_t
        // Convert to TsValue struct using nanbox_to_tagged
        return nanbox_to_tagged((TsValue*)(uintptr_t)(uint64_t)val);
    }

    // Value-based set - takes TsValue by value
    void ts_array_set_v(void* arr, int64_t index, TsValue value) {
        if (!arr) return;
        TsArray* array = (TsArray*)arr;
        ElementKind kind = array->GetElementKind();

        // ==== V8-style element kind fast paths ====

        // PackedSmi: Store as raw int64
        if (kind == ElementKind::PackedSmi || kind == ElementKind::HoleySmi) {
            if (value.type == ValueType::NUMBER_INT) {
                // Check SMI range
                if (value.i_val >= -1073741824LL && value.i_val <= 1073741823LL) {
                    array->Set(index, value.i_val);
                    return;
                }
                // Out of SMI range - transition to Double
                array->TransitionTo(ElementKind::PackedDouble);
                double d = (double)value.i_val;
                int64_t bits;
                memcpy(&bits, &d, sizeof(bits));
                array->Set(index, bits);
                return;
            } else if (value.type == ValueType::NUMBER_DBL) {
                // Double value - transition to PackedDouble
                array->TransitionTo(ElementKind::PackedDouble);
                int64_t bits;
                memcpy(&bits, &value.d_val, sizeof(bits));
                array->Set(index, bits);
                return;
            } else {
                // Non-numeric - transition to PackedAny
                array->TransitionTo(ElementKind::PackedAny);
                TsValue* nb = nanbox_from_tagged(value);
                array->Set(index, (int64_t)(uintptr_t)nb);
                return;
            }
        }

        // PackedDouble: Store as raw double bits
        if (kind == ElementKind::PackedDouble || kind == ElementKind::HoleyDouble) {
            if (value.type == ValueType::NUMBER_DBL) {
                int64_t bits;
                memcpy(&bits, &value.d_val, sizeof(bits));
                array->Set(index, bits);
                return;
            } else if (value.type == ValueType::NUMBER_INT) {
                double d = (double)value.i_val;
                int64_t bits;
                memcpy(&bits, &d, sizeof(bits));
                array->Set(index, bits);
                return;
            } else {
                // Non-numeric - transition to PackedAny
                array->TransitionTo(ElementKind::PackedAny);
                TsValue* nb = nanbox_from_tagged(value);
                array->Set(index, (int64_t)(uintptr_t)nb);
                return;
            }
        }

        // ==== Legacy specialized arrays (backward compat) ====
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
            return;
        }

        // ==== PackedAny / generic path ====
        // Store NaN-boxed value directly (no heap allocation)
        TsValue* nb = nanbox_from_tagged(value);
        array->Set(index, (int64_t)(uintptr_t)nb);
    }

    // Helper to extract raw TsArray* from potentially NaN-boxed TsValue*
    static TsArray* unboxArrayIfNeeded(void* arr) {
        if (!arr) return nullptr;

        // With NaN boxing, arr may be a NaN-boxed pointer (= raw pointer)
        // Use ts_value_get_object to decode
        void* raw = ts_value_get_object((TsValue*)arr);
        if (raw) return (TsArray*)raw;

        // Fallback: treat as raw array pointer
        return (TsArray*)arr;
    }

    void* ts_array_get_unchecked(void* arr, int64_t index) {
        TsArray* array = unboxArrayIfNeeded(arr);
        if (!array) return nullptr;
        if (index < 0 || (size_t)index >= (size_t)array->Length()) return nullptr;
        return (void*)array->GetUnchecked(index);
    }

    void ts_array_set_unchecked(void* arr, int64_t index, void* value) {
        TsArray* array = unboxArrayIfNeeded(arr);
        if (!array) return;
        array->SetUnchecked(index, (int64_t)value);
    }

    int64_t ts_array_length(void* arr) {
        if (!arr) return 0;

        // With NaN boxing, arr may be a NaN-boxed pointer
        // Use ts_value_get_object to decode
        void* raw = ts_value_get_object((TsValue*)arr);
        if (raw) arr = raw;
        else if ((uint64_t)(uintptr_t)arr < 0x10000) return 0;  // NaN-boxed special (undefined=0x0A, null=0x02, etc.)

        // Check magic to handle TsRegExpMatchArray
        uint32_t magic = *(uint32_t*)arr;
        if (magic == 0x524D4154) { // TsRegExpMatchArray::MAGIC ("RMAT")
            return ((TsRegExpMatchArray*)arr)->Length();
        }
        return ((TsArray*)arr)->Length();
    }

    bool ts_array_isArray(void* value) {
        if (!value) return false;

        // Decode NaN-boxed value
        TsValue decoded = nanbox_to_tagged((TsValue*)value);
        if (decoded.type == ValueType::ARRAY_PTR) {
            return true;
        }
        if (decoded.type == ValueType::OBJECT_PTR && decoded.ptr_val) {
            uint32_t magic = *(uint32_t*)decoded.ptr_val;
            return magic == TsArray::MAGIC || magic == 0x524D4154; // TsRegExpMatchArray
        }
        return false;
    }

    // Thread-local comparator state for use in std::sort
    static thread_local void* g_current_comparator = nullptr;
    static thread_local bool g_comparator_is_closure = false;

    // Comparator wrapper for arrays with boxed elements (TsValue* stored as int64)
    static bool comparator_wrapper_boxed(int64_t a, int64_t b) {
        if (!g_current_comparator) return a < b;

        // a and b are actually TsValue* pointers stored as int64
        TsValue* aVal = (TsValue*)a;
        TsValue* bVal = (TsValue*)b;

        TsValue* result;
        if (g_comparator_is_closure) {
            // HIR-generated closure path
            result = ts_closure_invoke_2v((TsClosure*)g_current_comparator, aVal, bVal);
        } else {
            // Standard TsValue/TsFunction path
            result = ts_call_2((TsValue*)g_current_comparator, aVal, bVal);
        }
        if (!result) return a < b;

        // Get the result as an int (negative = a < b, zero = equal, positive = a > b)
        int64_t cmp = ts_value_get_int(result);
        return cmp < 0;
    }

    // Comparator wrapper for raw int64 arrays
    static bool comparator_wrapper_int(int64_t a, int64_t b) {
        if (!g_current_comparator) return a < b;

        // Create boxed values for a and b
        TsValue* aVal = ts_value_make_int(a);
        TsValue* bVal = ts_value_make_int(b);

        TsValue* result;
        if (g_comparator_is_closure) {
            // HIR-generated closure path
            result = ts_closure_invoke_2v((TsClosure*)g_current_comparator, aVal, bVal);
        } else {
            // Standard TsValue/TsFunction path
            result = ts_call_2((TsValue*)g_current_comparator, aVal, bVal);
        }
        if (!result) return a < b;

        // Get the result as an int (negative = a < b, zero = equal, positive = a > b)
        int64_t cmp = ts_value_get_int(result);
        return cmp < 0;
    }

    // ts_array_sort - HIR calls this with (array, comparator) and expects array to be returned
    void* ts_array_sort(void* arr, void* comparator) {
        TsArray* array = (TsArray*)arr;

        if (!comparator) {
            array->Sort();
            return array;
        }

        // Check if comparator is a TsClosure (from HIR path)
        if (ts_is_closure(comparator)) {
            g_current_comparator = comparator;
            g_comparator_is_closure = true;
        } else {
            g_current_comparator = comparator;
            g_comparator_is_closure = false;
        }

        int64_t* elements = (int64_t*)array->GetElementsPtr();
        size_t len = array->Length();

        // HIR creates arrays with boxed TsValue* elements, use boxed wrapper
        // Non-specialized arrays store TsValue* as int64
        std::sort(elements, elements + len, comparator_wrapper_boxed);

        g_current_comparator = nullptr;
        g_comparator_is_closure = false;

        return array;  // Return the array (sort mutates in place)
    }

    // Legacy function - keep for backward compatibility
    void ts_array_sort_with_comparator(void* arr, void* comparator) {
        ts_array_sort(arr, comparator);
    }

    void* ts_array_slice(void* arr, int64_t start, int64_t end) {
        return ((TsArray*)arr)->Slice(start, end);
    }

    int64_t ts_array_indexOf(void* arr, int64_t value) {
        return ((TsArray*)arr)->IndexOf(value);
    }

    int64_t ts_array_lastIndexOf(void* arr, int64_t value) {
        return ((TsArray*)arr)->LastIndexOf(value);
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
        TsArray* a = (TsArray*)arr;
        int64_t len = a->Length();
        // Handle negative indices
        if (index < 0) index = len + index;
        if (index < 0 || index >= len) {
            return ts_value_make_undefined();
        }
        // Return a properly boxed value based on array type
        return a->GetElementBoxed(index);
    }

    void* ts_array_join(void* arr, void* separator) {
        // Unbox if arr is a TsValue* (boxed array)
        void* rawArr = ts_nanbox_safe_unbox(arr);
        return ((TsArray*)rawArr)->Join(separator);
    }

    void* ts_array_reverse(void* arr) {
        ((TsArray*)arr)->Reverse();
        return arr;
    }

    // ES2023 "change array by copy" methods
    void* ts_array_toReversed(void* arr) {
        return ((TsArray*)arr)->ToReversed();
    }

    void* ts_array_toSorted(void* arr) {
        return ((TsArray*)arr)->ToSorted();
    }

    void* ts_array_toSpliced(void* arr, int64_t start, int64_t deleteCount, void* items, int64_t itemCount) {
        // items is a TsArray*, we need to pass it as-is so ToSpliced can properly extract values
        return ((TsArray*)arr)->ToSpliced(start, deleteCount, items, itemCount);
    }

    void* ts_array_with(void* arr, int64_t index, void* value) {
        return ((TsArray*)arr)->With(index, (int64_t)value);
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

    void* ts_array_reduceRight(void* arr, void* callback, void* initialValue) {
        return ((TsArray*)arr)->ReduceRight(callback, initialValue);
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

    TsValue* ts_array_findLast(void* arr, void* callback, void* thisArg) {
        return ((TsArray*)arr)->FindLast(callback, thisArg);
    }

    int64_t ts_array_findLastIndex(void* arr, void* callback, void* thisArg) {
        return ((TsArray*)arr)->FindLastIndex(callback, thisArg);
    }

    void* ts_array_concat(void* arr, void* other) {
        // Unbox if arr is a TsValue* (boxed array)
        void* rawArr = ts_nanbox_safe_unbox(arr);
        void* rawOther = ts_nanbox_safe_unbox(other);

        TsArray* first = (TsArray*)rawArr;
        TsArray* second = (TsArray*)rawOther;

        // Create a new array with combined size
        size_t totalLen = first->Length() + second->Length();
        TsArray* result = TsArray::Create(totalLen);

        // Copy elements from first array
        for (size_t i = 0; i < first->Length(); ++i) {
            result->Push(first->Get(i));
        }

        // Copy elements from second array
        for (size_t i = 0; i < second->Length(); ++i) {
            result->Push(second->Get(i));
        }

        return result;
    }

    bool ts_array_is_array(void* value) {
        if (!value) return false;

        // Decode NaN-boxed value
        TsValue decoded = nanbox_to_tagged((TsValue*)value);
        if (decoded.type == ValueType::ARRAY_PTR && decoded.ptr_val) {
            uint32_t* magic_ptr = (uint32_t*)decoded.ptr_val;
            return *magic_ptr == TsArray::MAGIC;
        }

        return false;
    }

    void* ts_array_of(void* elementsArray) {
        // Array.of(...elements) - PackArray lowering already created the array
        // with all elements pushed in, so just return it as-is
        if (!elementsArray) return ts_array_create();
        return elementsArray;
    }

    void* ts_array_from(void* arrayLike, void* mapFn, void* thisArg) {
        if (!arrayLike) {
            return ts_array_create();
        }

        // Unbox if it's a TsValue*
        void* rawPtr = ts_nanbox_safe_unbox(arrayLike);

        // Get map function as TsValue* for calling (NaN-boxed)
        TsValue* mapFnVal = (TsValue*)mapFn;
        // With NaN boxing, a non-null function pointer is valid if it's a pointer above special range
        uint64_t mapFnNB = (uint64_t)(uintptr_t)mapFn;
        bool hasMapFn = mapFnVal && nanbox_is_ptr(mapFnNB);

        // Check if it's already an array
        if (ts_array_is_array(rawPtr)) {
            TsArray* srcArr = (TsArray*)rawPtr;
            TsArray* result = TsArray::Create(srcArr->Length());

            for (int64_t i = 0; i < srcArr->Length(); i++) {
                TsValue elem = ts_array_get_v(srcArr, i);

                if (hasMapFn) {
                    // Call the map function: mapFn(elem, index)
                    TsValue* elemBoxed = ts_value_box_any(&elem);
                    TsValue* indexVal = ts_value_make_int(i);
                    TsValue* mapped = ts_call_2(mapFnVal, elemBoxed, indexVal);
                    if (mapped) {
                        result->Push((int64_t)mapped);
                    } else {
                        ts_array_set_v(result, i, elem);
                    }
                } else {
                    ts_array_set_v(result, i, elem);
                }
            }
            return result;
        }

        // Check if it's a string - convert to character array
        // With NaN boxing, arrayLike is a NaN-boxed pointer to a TsString
        uint64_t alNB = (uint64_t)(uintptr_t)arrayLike;
        if (nanbox_is_ptr(alNB) && nanbox_to_ptr(alNB) &&
            *(uint32_t*)nanbox_to_ptr(alNB) == TsString::MAGIC) {
            TsString* str = (TsString*)nanbox_to_ptr(alNB);
            const char* utf8 = str->ToUtf8();
            int64_t len = str->Length();

            TsArray* result = TsArray::Create(len);
            for (int64_t i = 0; i < len; i++) {
                // Get each character as a string
                char charBuf[5] = {0}; // UTF-8 char can be up to 4 bytes

                // Simple ASCII extraction for now
                // TODO: proper Unicode codepoint extraction
                const char* p = utf8;
                int64_t idx = 0;
                while (*p && idx < i) {
                    if ((*p & 0x80) == 0) p += 1;
                    else if ((*p & 0xE0) == 0xC0) p += 2;
                    else if ((*p & 0xF0) == 0xE0) p += 3;
                    else if ((*p & 0xF8) == 0xF0) p += 4;
                    else p += 1;
                    idx++;
                }

                if (*p) {
                    if ((*p & 0x80) == 0) { charBuf[0] = *p; }
                    else if ((*p & 0xE0) == 0xC0) { memcpy(charBuf, p, 2); }
                    else if ((*p & 0xF0) == 0xE0) { memcpy(charBuf, p, 3); }
                    else if ((*p & 0xF8) == 0xF0) { memcpy(charBuf, p, 4); }
                }

                TsString* charStr = TsString::Create(charBuf);
                TsValue* charVal = ts_value_make_string(charStr);

                if (hasMapFn) {
                    TsValue* indexVal = ts_value_make_int(i);
                    TsValue* mapped = ts_call_2(mapFnVal, charVal, indexVal);
                    if (mapped) {
                        result->Push((int64_t)mapped);
                    } else {
                        result->Push((int64_t)charVal);
                    }
                } else {
                    result->Push((int64_t)charVal);
                }
            }
            return result;
        }

        // Check if it's an object with a 'length' property (array-like)
        if (is_flat_object(rawPtr)) {
            rawPtr = ts_flat_object_to_map(rawPtr);
        }
        TsMap* map = dynamic_cast<TsMap*>((TsObject*)rawPtr);
        if (map) {
            // Try to get 'length' property
            TsValue lengthKey;
            lengthKey.type = ValueType::STRING_PTR;
            lengthKey.ptr_val = TsString::Create("length");
            TsValue lengthVal = map->Get(lengthKey);

            if (lengthVal.type == ValueType::NUMBER_INT || lengthVal.type == ValueType::NUMBER_DBL) {
                int64_t len = (lengthVal.type == ValueType::NUMBER_INT) ?
                              lengthVal.i_val : (int64_t)lengthVal.d_val;

                if (len > 0) {
                    TsArray* result = TsArray::Create(len);

                    for (int64_t i = 0; i < len; i++) {
                        // Get element at index i
                        TsValue indexKey;
                        indexKey.type = ValueType::NUMBER_INT;
                        indexKey.i_val = i;
                        TsValue elem = map->Get(indexKey);

                        // Also try string key (some array-likes use string keys)
                        if (elem.type == ValueType::UNDEFINED) {
                            char buf[32];
                            snprintf(buf, sizeof(buf), "%lld", (long long)i);
                            TsValue strKey;
                            strKey.type = ValueType::STRING_PTR;
                            strKey.ptr_val = TsString::Create(buf);
                            elem = map->Get(strKey);
                        }

                        if (hasMapFn) {
                            TsValue* elemBoxed = ts_value_box_any(&elem);
                            TsValue* indexVal = ts_value_make_int(i);
                            TsValue* mapped = ts_call_2(mapFnVal, elemBoxed, indexVal);
                            if (mapped) {
                                result->Push((int64_t)mapped);
                            } else {
                                ts_array_set_v(result, i, elem);
                            }
                        } else {
                            ts_array_set_v(result, i, elem);
                        }
                    }
                    return result;
                }
            }
        }

        // Fallback: return empty array
        return ts_array_create();
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

        // Check for TsRegExpMatchArray (stores boxed TsValue* like non-specialized TsArray)
        uint32_t magic = *(uint32_t*)arr;
        if (magic == 0x524D4154) { // TsRegExpMatchArray::MAGIC ("RMAT")
            TsRegExpMatchArray* match = (TsRegExpMatchArray*)arr;
            if (index >= match->Length()) {
                *out_type = (uint8_t)ValueType::UNDEFINED;
                *out_value = 0;
                return;
            }
            // Elements are boxed TsValue* pointers
            int64_t raw_val = ((int64_t*)match->GetElementsPtr())[index];
            if (raw_val == 0) {
                *out_type = (uint8_t)ValueType::UNDEFINED;
                *out_value = 0;
                return;
            }
            TsValue decoded = nanbox_to_tagged((TsValue*)raw_val);
            *out_type = (uint8_t)decoded.type;
            *out_value = decoded.i_val;
            return;
        }

        TsArray* array = (TsArray*)arr;
        if (index >= array->Length()) {
            *out_type = (uint8_t)ValueType::UNDEFINED;
            *out_value = 0;
            return;
        }

        int64_t raw_val = array->GetUnchecked(index);
        ElementKind kind = array->GetElementKind();

        // ==== V8-style element kind fast paths ====

        // PackedSmi: Return as NUMBER_INT directly
        if (kind == ElementKind::PackedSmi || kind == ElementKind::HoleySmi) {
            *out_type = (uint8_t)ValueType::NUMBER_INT;
            *out_value = raw_val;
            return;
        }

        // PackedDouble: Return as NUMBER_DBL directly
        if (kind == ElementKind::PackedDouble || kind == ElementKind::HoleyDouble) {
            *out_type = (uint8_t)ValueType::NUMBER_DBL;
            *out_value = raw_val;  // Double bits stored as int64
            return;
        }

        // ==== Legacy specialized arrays (backward compat) ====

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

        // For non-specialized arrays, stored values are NaN-boxed uint64_t.
        // Decode using NaN-box helpers.
        uint64_t nb = (uint64_t)raw_val;

        if (nanbox_is_undefined(nb)) {
            *out_type = (uint8_t)ValueType::UNDEFINED;
            *out_value = 0;
            return;
        }
        if (nanbox_is_null(nb)) {
            *out_type = (uint8_t)ValueType::OBJECT_PTR;
            *out_value = 0;
            return;
        }
        if (nanbox_is_int32(nb)) {
            *out_type = (uint8_t)ValueType::NUMBER_INT;
            *out_value = (int64_t)nanbox_to_int32(nb);
            return;
        }
        if (nanbox_is_double(nb)) {
            *out_type = (uint8_t)ValueType::NUMBER_DBL;
            double d = nanbox_to_double(nb);
            memcpy(out_value, &d, sizeof(d));
            return;
        }
        if (nanbox_is_bool(nb)) {
            *out_type = (uint8_t)ValueType::BOOLEAN;
            *out_value = nanbox_to_bool(nb) ? 1 : 0;
            return;
        }
        if (nanbox_is_ptr(nb)) {
            void* ptr = nanbox_to_ptr(nb);
            // Read magic to determine pointer sub-type
            uint32_t magic0 = *(uint32_t*)ptr;
            if (magic0 == 0x53545247) { // TsString::MAGIC
                *out_type = (uint8_t)ValueType::STRING_PTR;
                *out_value = (int64_t)(uintptr_t)ptr;
                return;
            }
            if (magic0 == 0x41525259) { // TsArray::MAGIC
                *out_type = (uint8_t)ValueType::ARRAY_PTR;
                *out_value = (int64_t)(uintptr_t)ptr;
                return;
            }
            *out_type = (uint8_t)ValueType::OBJECT_PTR;
            *out_value = (int64_t)(uintptr_t)ptr;
            return;
        }

        // Fallback: treat as raw integer value
        *out_type = (uint8_t)ValueType::NUMBER_INT;
        *out_value = raw_val;
    }
    
    // Set array element from separate type/value
    // For non-specialized arrays, we need to store TsValue* pointers, not raw values
    void __ts_array_set_inline(void* arr, int64_t index, uint8_t val_type, int64_t val_value) {
        if (!arr || index < 0) return;

        TsArray* array = (TsArray*)arr;
        ElementKind kind = array->GetElementKind();
        ValueType vtype = (ValueType)val_type;

        // ==== V8-style element kind fast paths ====

        // PackedSmi: Store as raw int64
        if (kind == ElementKind::PackedSmi || kind == ElementKind::HoleySmi) {
            if (vtype == ValueType::NUMBER_INT) {
                // Check SMI range
                if (val_value >= -1073741824LL && val_value <= 1073741823LL) {
                    array->Set(index, val_value);
                    return;
                }
                // Out of SMI range - transition to Double
                array->TransitionTo(ElementKind::PackedDouble);
                double dval = (double)val_value;
                int64_t bits;
                memcpy(&bits, &dval, sizeof(bits));
                array->Set(index, bits);
                return;
            } else if (vtype == ValueType::NUMBER_DBL) {
                // Double value - transition to PackedDouble
                array->TransitionTo(ElementKind::PackedDouble);
                array->Set(index, val_value);  // Already double bits
                return;
            } else {
                // Non-numeric - transition to PackedAny
                array->TransitionTo(ElementKind::PackedAny);
                TsValue tv;
                tv.type = vtype;
                tv.i_val = val_value;
                TsValue* nb = nanbox_from_tagged(tv);
                array->Set(index, (int64_t)(uintptr_t)nb);
                return;
            }
        }

        // PackedDouble: Store as raw double bits
        if (kind == ElementKind::PackedDouble || kind == ElementKind::HoleyDouble) {
            if (vtype == ValueType::NUMBER_DBL) {
                array->Set(index, val_value);  // Double bits stored as int64
                return;
            } else if (vtype == ValueType::NUMBER_INT) {
                // Convert int to double
                double dval = (double)val_value;
                int64_t bits;
                memcpy(&bits, &dval, sizeof(bits));
                array->Set(index, bits);
                return;
            } else {
                // Non-numeric - transition to PackedAny
                array->TransitionTo(ElementKind::PackedAny);
                // Reconstruct NaN-boxed value from type and value
                TsValue tv;
                tv.type = vtype;
                tv.i_val = val_value;
                TsValue* nb = nanbox_from_tagged(tv);
                array->Set(index, (int64_t)(uintptr_t)nb);
                return;
            }
        }

        // ==== Legacy specialized arrays (backward compat) ====

        // For specialized arrays, store raw value
        if (array->IsSpecialized()) {
            array->Set(index, val_value);
            return;
        }

        // ==== PackedAny / generic path ====

        // Reconstruct NaN-boxed value from type and value (no heap allocation)
        TsValue tv;
        tv.type = vtype;
        tv.i_val = val_value;
        TsValue* nb = nanbox_from_tagged(tv);
        array->Set(index, (int64_t)(uintptr_t)nb);
    }
    
    // Get array length (ensure it's exported for inline IR)
    int64_t __ts_array_length(void* arr) {
        if (!arr) return 0;
        return ((TsArray*)arr)->Length();
    }

    // Fill array with a value from start to end index
    // Returns the modified array (for chaining)
    void* ts_array_fill(void* arr, void* value, int64_t start, int64_t end) {
        if (!arr) return arr;

        TsArray* array = (TsArray*)arr;
        int64_t len = (int64_t)array->Length();

        // Handle negative indices and clamp to valid range
        if (start < 0) start = std::max((int64_t)0, len + start);
        if (end < 0) end = std::max((int64_t)0, len + end);
        if (start >= len) return arr;
        if (end > len) end = len;
        if (start >= end) return arr;

        // Fill the range with the value
        for (int64_t i = start; i < end; i++) {
            array->Set(i, (int64_t)value);
        }

        return arr;
    }

    // Copy a sequence of array elements within the array to another position
    // Returns the modified array (for chaining)
    void* ts_array_copyWithin(void* arr, int64_t target, int64_t start, int64_t end) {
        if (!arr) return arr;

        TsArray* array = (TsArray*)arr;
        int64_t len = (int64_t)array->Length();

        // Handle negative indices
        if (target < 0) target = std::max((int64_t)0, len + target);
        if (start < 0) start = std::max((int64_t)0, len + start);
        if (end < 0) end = std::max((int64_t)0, len + end);

        // Clamp to valid range
        if (target >= len) return arr;
        if (start >= len) return arr;
        if (end > len) end = len;
        if (start >= end) return arr;

        // Calculate the number of elements to copy
        int64_t count = std::min(end - start, len - target);
        if (count <= 0) return arr;

        // Copy elements (handle overlapping regions correctly)
        if (start < target && target < start + count) {
            // Overlapping: copy backwards to avoid overwriting
            for (int64_t i = count - 1; i >= 0; i--) {
                int64_t val = array->Get(start + i);
                array->Set(target + i, val);
            }
        } else {
            // Non-overlapping or source after target: copy forwards
            for (int64_t i = 0; i < count; i++) {
                int64_t val = array->Get(start + i);
                array->Set(target + i, val);
            }
        }

        return arr;
    }

    // Returns an array of [index, value] pairs (iterator-like behavior)
    void* ts_array_entries(void* arr) {
        if (!arr) return nullptr;

        TsArray* array = (TsArray*)arr;
        int64_t len = (int64_t)array->Length();
        TsArray* entries = TsArray::Create(len);

        for (int64_t i = 0; i < len; i++) {
            // Create [index, value] pair as a 2-element array
            TsArray* pair = TsArray::Create(2);

            // Box the index as NaN-boxed int
            pair->Push((int64_t)(uintptr_t)ts_value_make_int(i));

            // Get the value (already NaN-boxed for non-specialized arrays)
            int64_t val = array->Get(i);
            pair->Push(val);

            // Box the pair array as NaN-boxed pointer
            entries->Push((int64_t)(uintptr_t)pair);
        }

        return entries;
    }

    // Returns an array of indices (iterator-like behavior)
    void* ts_array_keys(void* arr) {
        if (!arr) return nullptr;

        TsArray* array = (TsArray*)arr;
        int64_t len = (int64_t)array->Length();
        TsArray* keys = TsArray::Create(len);

        for (int64_t i = 0; i < len; i++) {
            // Box the index as NaN-boxed int
            keys->Push((int64_t)(uintptr_t)ts_value_make_int(i));
        }

        return keys;
    }

    // Returns an array of values (iterator-like behavior)
    void* ts_array_values(void* arr) {
        if (!arr) return nullptr;

        TsArray* array = (TsArray*)arr;
        int64_t len = (int64_t)array->Length();
        TsArray* values = TsArray::Create(len);

        for (int64_t i = 0; i < len; i++) {
            int64_t val = array->Get(i);
            values->Push(val);
        }

        return values;
    }

    // ============================================================
    // Element Kind API (V8-style optimization)
    // ============================================================

    uint8_t ts_array_get_element_kind(void* arr) {
        if (!arr) return (uint8_t)ElementKind::Unknown;
        return (uint8_t)((TsArray*)arr)->GetElementKind();
    }

    void ts_array_set_element_kind(void* arr, uint8_t kind) {
        if (!arr) return;
        ((TsArray*)arr)->SetElementKind((ElementKind)kind);
    }

    void ts_array_transition_to(void* arr, uint8_t newKind) {
        if (!arr) return;
        ((TsArray*)arr)->TransitionTo((ElementKind)newKind);
    }

    void* ts_array_create_with_kind(int64_t size, uint8_t kind) {
        TsArray* arr;
        ElementKind ek = (ElementKind)kind;

        switch (ek) {
            case ElementKind::PackedSmi:
            case ElementKind::HoleySmi:
                // SMI arrays use 8-byte int64_t storage (could optimize to 4-byte later)
                arr = TsArray::CreateSpecialized(size, 8, false);
                break;
            case ElementKind::PackedDouble:
            case ElementKind::HoleyDouble:
                // Double arrays use 8-byte double storage
                arr = TsArray::CreateSpecialized(size, 8, true);
                break;
            default:
                // Generic arrays use 8-byte pointer storage
                arr = TsArray::CreateSized(size);
                break;
        }
        arr->SetElementKind(ek);
        return arr;
    }

    // Fast SMI get - no boxing, returns raw int64
    int64_t ts_array_get_smi(void* arr, int64_t index) {
        if (!arr) return 0;
        TsArray* array = (TsArray*)arr;
        if (index < 0 || index >= array->Length()) return 0;
        return array->GetUnchecked(index);
    }

    // Fast SMI set - no boxing
    void ts_array_set_smi(void* arr, int64_t index, int64_t value) {
        if (!arr) return;
        TsArray* array = (TsArray*)arr;
        if (index < 0) return;
        // Extend array if needed
        if (index >= array->Length()) {
            // For now, only allow setting at the next position
            if (index == array->Length()) {
                array->Push(value);
            }
            return;
        }
        array->SetUnchecked(index, value);
    }

    // Fast double get - no boxing
    double ts_array_get_double_fast(void* arr, int64_t index) {
        if (!arr) return 0.0;
        TsArray* array = (TsArray*)arr;
        if (index < 0 || index >= array->Length()) return 0.0;
        int64_t bits = array->GetUnchecked(index);
        double result;
        memcpy(&result, &bits, sizeof(result));
        return result;
    }

    // Fast double set - no boxing
    void ts_array_set_double_fast(void* arr, int64_t index, double value) {
        if (!arr) return;
        TsArray* array = (TsArray*)arr;
        if (index < 0) return;
        int64_t bits;
        memcpy(&bits, &value, sizeof(bits));
        if (index >= array->Length()) {
            if (index == array->Length()) {
                array->Push(bits);
            }
            return;
        }
        array->SetUnchecked(index, bits);
    }
}
