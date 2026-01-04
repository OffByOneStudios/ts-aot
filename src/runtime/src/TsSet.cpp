#include "TsSet.h"
#include "TsArray.h"
#include "TsObject.h"
#include "TsRuntime.h"
#include "TsBigInt.h"
#include "TsSymbol.h"
#include "GC.h"
#include <unordered_set>
#include <string>
#include <cstring>
#include <new>
#include <functional>
#include <iostream>
#include <cmath>

// Allocator
template <class T>
struct TsAllocator {
    typedef T value_type;
    TsAllocator() = default;
    template <class U> constexpr TsAllocator(const TsAllocator<U>&) noexcept {}
    T* allocate(std::size_t n) {
        if (n > std::size_t(-1) / sizeof(T)) throw std::bad_alloc();
        if (auto p = static_cast<T*>(ts_alloc(n * sizeof(T)))) return p;
        throw std::bad_alloc();
    }
    void deallocate(T* p, std::size_t) noexcept { }
};

// Hash and Equal for TsValue (SameValueZero)
struct TsValueHash {
    std::size_t operator()(const TsValue& v) const {
        switch (v.type) {
            case ValueType::UNDEFINED: return 0;
            case ValueType::NUMBER_INT: return std::hash<int64_t>{}(v.i_val);
            case ValueType::NUMBER_DBL: return std::hash<double>{}(v.d_val);
            case ValueType::BOOLEAN: return std::hash<bool>{}(v.b_val);
            case ValueType::STRING_PTR: {
                TsString* s = (TsString*)v.ptr_val;
                const char* str = s->ToUtf8();
                size_t h = 5381;
                int c;
                while ((c = *str++))
                    h = ((h << 5) + h) + c;
                return h;
            }
            case ValueType::BIGINT_PTR: {
                TsBigInt* bi = (TsBigInt*)v.ptr_val;
                const char* str = bi->ToString();
                size_t h = 5381;
                int c;
                while ((c = *str++))
                    h = ((h << 5) + h) + c;
                return h;
            }
            default: return std::hash<void*>{}(v.ptr_val);
        }
    }
};

struct TsValueEqual {
    bool operator()(const TsValue& lhs, const TsValue& rhs) const {
        if (lhs.type != rhs.type) return false;
        switch (lhs.type) {
            case ValueType::UNDEFINED: return true;
            case ValueType::NUMBER_INT: return lhs.i_val == rhs.i_val;
            case ValueType::NUMBER_DBL: {
                if (std::isnan(lhs.d_val) && std::isnan(rhs.d_val)) return true;
                return lhs.d_val == rhs.d_val;
            }
            case ValueType::BOOLEAN: return lhs.b_val == rhs.b_val;
            case ValueType::STRING_PTR: {
                return std::strcmp(((TsString*)lhs.ptr_val)->ToUtf8(), ((TsString*)rhs.ptr_val)->ToUtf8()) == 0;
            }
            case ValueType::BIGINT_PTR: {
                return std::strcmp(((TsBigInt*)lhs.ptr_val)->ToString(), ((TsBigInt*)rhs.ptr_val)->ToString()) == 0;
            }
            default: return lhs.ptr_val == rhs.ptr_val;
        }
    }
};

using SetType = std::unordered_set<TsValue, TsValueHash, TsValueEqual, TsAllocator<TsValue>>;

void* TsSet_VTable[2] = { nullptr, nullptr };
extern "C" TsValue* ts_set_get_property(void* obj, void* propName);

TsSet* TsSet::Create() {
    void* mem = ts_alloc(sizeof(TsSet));
    TsSet* set = new(mem) TsSet();
    
    if (!TsSet_VTable[1]) {
        TsSet_VTable[1] = (void*)ts_set_get_property;
    }
    set->vtable = TsSet_VTable;
    
    return set;
}

TsSet::TsSet() {
    TsObject::magic = MAGIC;  // Set base class magic for type detection
    void* mem = ts_alloc(sizeof(SetType));
    impl = new(mem) SetType();
}

void TsSet::Add(TsValue value) {
    ((SetType*)impl)->insert(value);
}

bool TsSet::Has(TsValue value) {
    SetType* set = static_cast<SetType*>(impl);
    return set->find(value) != set->end();
}

bool TsSet::Delete(TsValue value) {
    SetType* set = static_cast<SetType*>(impl);
    return set->erase(value) > 0;
}

void TsSet::Clear() {
    SetType* set = static_cast<SetType*>(impl);
    set->clear();
}

int64_t TsSet::Size() {
    SetType* set = static_cast<SetType*>(impl);
    return static_cast<int64_t>(set->size());
}

void* TsSet::GetValues() {
    SetType* set = static_cast<SetType*>(impl);
    TsArray* values = TsArray::Create(set->size());
    for (auto const& val : *set) {
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        *v = val;
        values->Push((int64_t)v);
    }
    return values;
}

void TsSet::ForEach(void* callback, void* thisArg) {
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal || cbVal->type != ValueType::FUNCTION_PTR) return;

    SetType* set = (SetType*)impl;
    for (auto const& val : *set) {
        TsValue* v1 = (TsValue*)ts_alloc(sizeof(TsValue));
        *v1 = val;
        TsValue* v2 = (TsValue*)ts_alloc(sizeof(TsValue));
        *v2 = val;
        TsValue* s = ts_value_make_object(this);
        ts_call_3(cbVal, v1, v2, s);
    }
}

extern "C" {

void* ts_set_create() {
    return TsSet::Create();
}

void ts_set_add(void* set, TsValue* value) {
    if (!set || !value) return;
    ((TsSet*)set)->Add(*value);
}

bool ts_set_has(void* set, TsValue* value) {
    if (!set || !value) return false;
    return ((TsSet*)set)->Has(*value);
}

bool ts_set_delete(void* set, TsValue* value) {
    if (!set || !value) return false;
    return ((TsSet*)set)->Delete(*value);
}

void ts_set_clear(void* set) {
    if (!set) return;
    ((TsSet*)set)->Clear();
}

int64_t ts_set_size(void* set) {
    if (!set) return 0;
    return ((TsSet*)set)->Size();
}

void* ts_set_values(void* set) {
    if (!set) return nullptr;
    return ((TsSet*)set)->GetValues();
}

void ts_set_forEach(void* set, void* callback, void* thisArg) {
    if (!set) return;
    ((TsSet*)set)->ForEach(callback, thisArg);
}

TsValue* ts_set_add_wrapper(void* context, TsValue* value) {
    ts_set_add(context, value);
    TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
    res->type = ValueType::OBJECT_PTR;
    res->ptr_val = context;
    return res;
}

TsValue* ts_set_has_wrapper(void* context, TsValue* value) {
    return ts_value_make_bool(ts_set_has(context, value));
}

TsValue* ts_set_delete_wrapper(void* context, TsValue* value) {
    return ts_value_make_bool(ts_set_delete(context, value));
}

TsValue* ts_set_clear_wrapper(void* context) {
    ts_set_clear(context);
    return ts_value_make_undefined();
}

TsValue* ts_set_size_wrapper(void* context) {
    return ts_value_make_int(ts_set_size(context));
}

TsValue* ts_set_get_property(void* obj, void* propName) {
    TsString* prop = (TsString*)propName;
    const char* name = prop->ToUtf8();
    
    if (strcmp(name, "add") == 0) {
        return ts_value_make_function((void*)ts_set_add_wrapper, obj);
    } else if (strcmp(name, "has") == 0) {
        return ts_value_make_function((void*)ts_set_has_wrapper, obj);
    } else if (strcmp(name, "delete") == 0) {
        return ts_value_make_function((void*)ts_set_delete_wrapper, obj);
    } else if (strcmp(name, "clear") == 0) {
        return ts_value_make_function((void*)ts_set_clear_wrapper, obj);
    } else if (strcmp(name, "size") == 0) {
        return ts_value_make_int(ts_set_size(obj));
    }
    
    return ts_value_make_undefined();
}

}
