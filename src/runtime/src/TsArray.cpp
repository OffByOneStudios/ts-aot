#include "TsArray.h"
#include "TsConsString.h"
#include "TsObject.h"
#include "TsNanBox.h"
#include "TsMap.h"
#include "TsRegExp.h"
#include "TsRuntime.h"
#include "TsClosure.h"
#include "TsFlatObject.h"
#include "TsBuffer.h"
#include "TsError.h"
#include "TsPromise.h"
#include "GC.h"
#include "TsGC.h"
#include <cstring>
#include <cmath>
#include <climits>
#include <iostream>
#include <new>
#include <algorithm>

// Array.prototype observability: when user code mutates Array.prototype
// via Object.defineProperty, iteration methods must walk the prototype
// chain per spec. We gate on a module-level version counter that gets
// bumped only when such a mutation happens. Common case stays fast.
extern "C" {
    TsMap* g_array_prototype_map = nullptr;
    uint64_t g_array_prototype_version = 0;
    // NoElementsProtector analogue: set once an indexed property is written to
    // Array.prototype. While 0, the C++ array builtins are spec-correct for any
    // array (a hole has nothing to inherit); when 1, holey arrays must bail to
    // the spec path so inherited indices fill their holes.
    uint8_t g_array_proto_has_indexed = 0;
    // Self-hosted Array.prototype.filter impl (TsBuiltinInstall.cpp), if the
    // prelude installed one — ts_array_filter delegates a direct call to it.
    extern void* g_selfhosted_filter;
    // Set when `delete Array.prototype[Symbol.iterator]` runs. The default
    // array iterator lives in a built-in fast path (not in the prototype map),
    // so its removal isn't otherwise observable; ts_iterator_get consults this
    // flag to throw TypeError per GetIterator.
    bool g_array_default_iterator_deleted = false;
    double ts_to_number(TsValue* v);  // defined in Primitives.cpp
    // ToInteger for an index arg: throws TypeError on Symbol/BigInt (Primitives.cpp).
    int64_t ts_to_index_integer(TsValue* v);
    // Like ts_to_index_integer but returns INT64_MIN for an omitted (null) or
    // undefined argument so the "argument not provided" default path is kept.
    int64_t ts_to_index_integer_or_sentinel(TsValue* v);
    // ToNumber for a fromIndex arg (throws on Symbol/BigInt), preserving +/-Inf.
    double ts_to_index_number_or(TsValue* v, double deflt);
}

extern "C" void ts_array_prototype_bump_version() {
    g_array_prototype_version++;
}

extern "C" bool ts_array_is_prototype_map(void* maybeMap) {
    return maybeMap && maybeMap == (void*)g_array_prototype_map;
}

// Helper: read property at numeric index from Array.prototype's TsMap.
// Returns nullptr (NaN-boxed? no: raw nullptr) if not found. If an
// accessor getter is found (__getter_<i> convention from TsObject.cpp),
// invokes it with `recv` as `this` and returns the result.
static TsValue* array_proto_get_at(void* recv, int64_t i) {
    if (!g_array_prototype_map) return nullptr;
    char idxKey[24];
    snprintf(idxKey, sizeof(idxKey), "%lld", (long long)i);
    // Check for accessor first (__getter_<N>)
    std::string getterKey = std::string("__getter_") + idxKey;
    TsValue gk; gk.type = ValueType::STRING_PTR;
    gk.ptr_val = TsString::Create(getterKey.c_str());
    TsValue gv = g_array_prototype_map->Get(gk);
    if (gv.type != ValueType::UNDEFINED) {
        TsValue* getterFn = nullptr;
        if (gv.type == ValueType::FUNCTION_PTR || gv.type == ValueType::OBJECT_PTR) {
            getterFn = ts_value_make_object(gv.ptr_val);
        }
        if (getterFn) {
            TsValue* recvBoxed = ts_value_make_object(recv);
            return ts_function_call_with_this(getterFn, recvBoxed, 0, nullptr);
        }
    }
    // Direct data property at that index
    TsValue dk; dk.type = ValueType::STRING_PTR;
    dk.ptr_val = TsString::Create(idxKey);
    TsValue dv = g_array_prototype_map->Get(dk);
    if (dv.type != ValueType::UNDEFINED) {
        return nanbox_from_tagged(dv);
    }
    return nullptr;
}

// Spec HasProperty(O, ToString(k)): own data-or-accessor OR inherited.
// For the slow-path only.
bool ts_array_has_property_at(TsArray* arr, int64_t i) {
    if (!arr) return false;
    if (i >= 0 && (size_t)i < (size_t)arr->Length() && !arr->IsHole((size_t)i)) {
        return true;
    }
    // Check Array.prototype for own data property or accessor (get or set).
    if (!g_array_prototype_map) return false;
    char idxKey[24];
    snprintf(idxKey, sizeof(idxKey), "%lld", (long long)i);
    std::string getterKey = std::string("__getter_") + idxKey;
    TsValue gk; gk.type = ValueType::STRING_PTR;
    gk.ptr_val = TsString::Create(getterKey.c_str());
    if (g_array_prototype_map->Has(gk)) return true;
    // Set-only accessor still makes HasProperty true.
    std::string setterKey = std::string("__setter_") + idxKey;
    TsValue sk; sk.type = ValueType::STRING_PTR;
    sk.ptr_val = TsString::Create(setterKey.c_str());
    if (g_array_prototype_map->Has(sk)) return true;
    TsValue dk; dk.type = ValueType::STRING_PTR;
    dk.ptr_val = TsString::Create(idxKey);
    return g_array_prototype_map->Has(dk);
}

// extern "C" shim so other TUs (e.g. the `in` operator in TsObject.cpp) can
// consult prototype-aware HasProperty without C++-mangling/linkage friction.
extern "C" bool ts_array_has_property_at_idx(void* arr, int64_t i) {
    return ts_array_has_property_at((TsArray*)arr, i);
}

// Spec Get(O, ToString(k)): if Array.prototype has an accessor for this
// index, invoke it (inherited getter takes precedence over own when own
// slot is absent). Otherwise fall back to own indexed slot.
TsValue* ts_array_get_property_at(TsArray* arr, int64_t i) {
    if (!arr) return ts_value_make_undefined();
    // Own indexed slot wins if present (not a hole).
    if (i >= 0 && (size_t)i < (size_t)arr->Length() && !arr->IsHole((size_t)i)) {
        return arr->GetElementBoxed((size_t)i);
    }
    // Inherited via Array.prototype.
    TsValue* v = array_proto_get_at((void*)arr, i);
    return v ? v : ts_value_make_undefined();
}

// extern "C" shim: prototype-aware indexed Get for other TUs (the generic
// `obj[k]` path in TsObject.cpp, reached when the receiver is statically typed
// as object/any so it does not use the typed-array fast get).
extern "C" TsValue* ts_array_get_property_at_idx(void* arr, int64_t i) {
    return ts_array_get_property_at((TsArray*)arr, i);
}

TsArray* TsArray::Create(size_t initialCapacity) {
    void* mem = ts_alloc(sizeof(TsArray));
    return new(mem) TsArray(initialCapacity, 8);
}

void TsArray::InitInPlace(void* mem, size_t initialCapacity) {
    if (!mem) return;
    new(mem) TsArray(initialCapacity, 8);
}

TsArray* TsArray::CreateSized(size_t size) {
    // Huge sizes (e.g. `new Array(2.1e9)`) must NOT eager-allocate the backing
    // buffer or we OOM. Create a sparse array: small dense buffer, logical
    // length = size, all indices read as holes via readSlot. The TsArray ctor
    // caps its eager allocation at kMaxDenseElements.
    if (size > kMaxDenseElements) {
        void* mem = ts_alloc(sizeof(TsArray));
        TsArray* arr = new(mem) TsArray(4, 8);  // small dense buffer
        // Hole-fill the small dense buffer so its in-range slots read as
        // holes (everything is a hole until written); beyond capacity is
        // implicit-hole via readSlot.
        int64_t* slots = (int64_t*)arr->elements;
        for (size_t i = 0; i < arr->capacity; ++i) slots[i] = (int64_t)NANBOX_HOLE;
        arr->length = size;                     // sparse: length >> capacity
        arr->elementKind_ = ElementKind::HoleyAny;
        arr->has_holes_ = true;                 // every index is a hole
        return arr;
    }
    void* mem = ts_alloc(sizeof(TsArray));
    TsArray* arr = new(mem) TsArray(size, 8);
    arr->length = size;
    // Fill slots with the NANBOX_HOLE sentinel so HasProperty returns
    // false for unwritten indices (spec: new Array(n) produces n holes).
    int64_t* slots = (int64_t*)arr->elements;
    const int64_t hole = (int64_t)NANBOX_HOLE;
    for (size_t i = 0; i < size; ++i) slots[i] = hole;
    arr->elementKind_ = ElementKind::HoleyAny;
    if (size > 0) arr->has_holes_ = true;       // new Array(n)/[,,] => n holes
    return arr;
}

// Per-index ACCESSOR support (Lever A): Object.defineProperty(arr, i, {get/set})
// stores the getter/setter in the array's `properties` side-map under the keys
// __arr_getter_<i> / __arr_setter_<i>, leaving the element slot a hole. These
// helpers let IsHole/Get treat such an index as a PRESENT accessor property.
// Returns the boxed function, or nullptr if no such accessor is defined.
static TsValue* array_index_accessor_fn(TsArray* a, size_t index, bool getter) {
    if (!a || !a->properties) return nullptr;
    char k[40];
    snprintf(k, sizeof(k), getter ? "__arr_getter_%zu" : "__arr_setter_%zu", index);
    TsValue kk; kk.type = ValueType::STRING_PTR; kk.ptr_val = TsString::GetInterned(k);
    if (!a->properties->Has(kk)) return nullptr;
    return nanbox_from_tagged(a->properties->Get(kk));
}
static inline bool array_index_has_accessor(TsArray* a, size_t index) {
    return array_index_accessor_fn(a, index, true) ||
           array_index_accessor_fn(a, index, false);
}

// A4: per-index accessor/writable WRITE interception. Returns true when the
// write to arr[index] was handled here — a setter was invoked, or the write was
// silently ignored for a getter-only accessor or a non-writable data index
// (recorded "__arr_attrs_<i>" with the writable bit 0x02 clear) — so the caller
// must NOT fall through to the element store. Gated on `properties` so plain
// arrays pay only one null check. `value` is a NaN-boxed TsValue*.
static bool array_index_write_intercept(TsArray* a, size_t index, void* value) {
    if (!a || !a->properties) return false;
    if (TsValue* setter = array_index_accessor_fn(a, index, false)) {
        extern TsValue* ts_call_with_this_1(TsValue* fn, TsValue* thisArg, TsValue* arg1);
        ts_call_with_this_1(setter, ts_value_make_array(a), (TsValue*)value);
        return true;
    }
    if (array_index_accessor_fn(a, index, true)) {
        return true;  // getter-only accessor: write ignored (no setter)
    }
    char k[40]; snprintf(k, sizeof(k), "__arr_attrs_%zu", index);
    TsValue kk; kk.type = ValueType::STRING_PTR; kk.ptr_val = TsString::GetInterned(k);
    if (a->properties->Has(kk)) {
        TsValue av = a->properties->Get(kk);
        if (!(((uint64_t)av.i_val) & 0x02)) return true;  // non-writable → ignore
    }
    return false;
}

// Generic array-like receiver support: when an Array.prototype iteration method
// is invoked via .call() on a NON-array array-like object, require_array_or_throw
// (TsObject.cpp) materializes it into a temp TsArray (absent indices filled with
// undefined) and records the original object in `originalReceiver`. Spec methods
// that SKIP absent indices (forEach/map/filter/some/every/find/reduce/...) must
// consult HasProperty on the ORIGINAL receiver per index — the dense materialized
// array can't represent absent-vs-undefined (a blanket hole-preserving materializer
// netted -42; see the note in require_array_or_throw). Returns true when index i is
// ABSENT on the original generic-object receiver and must be skipped. For a normal
// array (no originalReceiver) or a string/primitive receiver (all indices present)
// it returns false, so the only cost on the common path is one pointer-null check.
bool array_generic_absent_index(const TsArray* self, size_t i) {
    void* orig = self->originalReceiver;
    if (!orig || orig == (const void*)self) return false;
    if ((uintptr_t)orig < 0x1000 || (uintptr_t)orig >= 0x0000800000000000ULL) return false;
    uint32_t m = *(uint32_t*)orig;
    if (m == 0x53545247 /* STRG */ || m == TsConsString::MAGIC) return false;  // string
    // TypedArray receiver: indices [0,length) are always dense/present, and
    // ts_object_has_prop currently reports false for typed-array numeric indices
    // (a separate has_prop gap) — without this guard every typed-array element
    // would be wrongly skipped (this was the G1 -17 regression). Never skip.
    if (*(uint32_t*)((char*)orig + 16) == 0x54415252 /* TsTypedArray "TARR" */) return false;
    extern bool ts_object_has_prop(TsValue* obj, TsValue* key);
    TsValue* objB = ts_value_make_object(orig);
    TsValue* keyB = ts_value_make_int((int64_t)i);
    return !ts_object_has_prop(objB, keyB);
}

bool TsArray::IsHole(size_t index) const {
    if (index >= length) return true;
    // An accessor defined via Object.defineProperty makes the index a PRESENT
    // own property. Check this FIRST and kind-agnostically (the element-kind
    // guards below short-circuit for HoleySmi/HoleyDouble where the hole isn't
    // a NANBOX_HOLE slot). Gated on `properties` so the common no-side-map array
    // pays only a pointer-null check.
    if (properties && array_index_has_accessor(const_cast<TsArray*>(this), index))
        return false;
    // Only the PackedAny / HoleyAny paths store NaN-box values directly.
    // Specialized (double/int) and SMI / Double element kinds can't
    // represent holes — the array would have been transitioned away from
    // those kinds before any hole was introduced.
    if (isSpecialized) return false;
    if (elementKind_ == ElementKind::PackedSmi ||
        elementKind_ == ElementKind::PackedDouble ||
        elementKind_ == ElementKind::HoleySmi ||
        elementKind_ == ElementKind::HoleyDouble) {
        return false;
    }
    int64_t raw = readSlot(index);
    return (uint64_t)raw == NANBOX_HOLE;
}

void TsArray::SetHole(size_t index) {
    if (index >= length) return;
    // Transition to HoleyAny so future reads/iteration know holes exist.
    if (elementKind_ != ElementKind::HoleyAny) {
        elementKind_ = ElementKind::HoleyAny;
    }
    has_holes_ = true;
    ((int64_t*)elements)[index] = (int64_t)NANBOX_HOLE;
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

    // Per-index accessor (Lever A): __arr_getter_<i>/__arr_setter_<i> in the
    // properties side-map governs reads regardless of element kind. This is the
    // boxed-read chokepoint used by the iteration methods (reduce/reduceRight/
    // forEach/map/filter/...) and ts_array_get_property_at. Gated on `properties`.
    if (properties) {
        if (TsValue* getter = array_index_accessor_fn(this, index, true)) {
            extern TsValue* ts_call_with_this_0(TsValue* boxedFunc, TsValue* thisArg);
            return ts_call_with_this_0(getter, ts_value_make_array(this));
        }
        if (array_index_accessor_fn(this, index, false)) {
            return ts_value_make_undefined();  // setter-only accessor
        }
    }

    if (isSpecialized) {
        if (isDouble) {
            double val = ((double*)elements)[index];
            return ts_value_make_double(val);
        } else {
            int64_t val = ((int64_t*)elements)[index];
            return ts_value_make_int(val);
        }
    }

    int64_t val = readSlot(index);
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

    // PackedAny / HoleyAny: stored values are NaN-boxed uint64_t.
    // Hole sentinel reads as undefined (spec: Get(arr, i) for a missing
    // index returns undefined after the prototype chain is consulted —
    // no accessors fire in the version-0 fast path).
    if ((uint64_t)val == NANBOX_HOLE) {
        return ts_value_make_undefined();
    }
    // Otherwise the stored value IS the NaN-boxed representation.
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
        // Dirty cards for copied elements (may contain nursery pointers)
        ts_gc_write_barrier_range(newElements, length * elementSize);
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
    uint64_t nb = (uint64_t)readSlot(index);
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
            // Dirty cards for copied elements (may contain nursery pointers)
            ts_gc_write_barrier_range(newElements, length * elementSize);
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
        // Dirty cards for copied elements (may contain nursery pointers)
        ts_gc_write_barrier_range((char*)newElements + elementSize, length * elementSize);
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

// Capacity-safe element read. For a dense array (index < capacity) this is
// identical to the raw slot. For a sparse array, an index in [capacity, length)
// is a hole unless present in the overflow store. Callers interpret the
// returned raw int64 per element kind (sparse arrays are always HoleyAny, so
// the value is a NaN-boxed uint64 / NANBOX_HOLE).
int64_t TsArray::readSlot(size_t index) const {
    if (index < capacity) {
        return ((int64_t*)elements)[index];
    }
    if (sparseElements) {
        TsMap* se = sparseElements;  // pointee is non-const in this const method
        TsValue key((int64_t)index);
        if (se->Has(key)) {
            TsValue v = se->Get(key);
            return (int64_t)(uintptr_t)nanbox_from_tagged(v);
        }
    }
    return (int64_t)NANBOX_HOLE;
}

// Capacity-safe element write. Dense slot if index < capacity; bounded dense
// growth if index < kMaxDenseElements; otherwise spill to the sparse store.
// Never eager-allocates more than kMaxDenseElements slots, so it cannot OOM on
// a huge sparse index. Assumes 8-byte slots (specialized arrays stay dense and
// never reach the index >= capacity branches).
void TsArray::writeSlot(size_t index, int64_t value) {
    if (index < capacity) {
        ((int64_t*)elements)[index] = value;
        ts_gc_write_barrier(&((int64_t*)elements)[index], (void*)value);
        return;
    }
    if (index < kMaxDenseElements) {
        // Bounded dense growth: reallocate to cover `index`, hole-fill the gap.
        size_t oldCap = capacity;
        size_t newCapacity = oldCap ? oldCap : 4;
        while (newCapacity <= index) newCapacity *= 2;
        if (newCapacity > kMaxDenseElements) newCapacity = kMaxDenseElements;
        void* newElements = ts_alloc(newCapacity * elementSize);
        if (oldCap > 0) std::memcpy(newElements, elements, oldCap * elementSize);
        for (size_t i = oldCap; i < newCapacity; i++) {
            ((int64_t*)newElements)[i] = (int64_t)NANBOX_HOLE;
        }
        elements = newElements;
        ts_gc_write_barrier((void*)&this->elements, newElements);
        ts_gc_write_barrier_range(newElements, oldCap * elementSize);
        capacity = newCapacity;
        if (elementKind_ != ElementKind::HoleyAny) elementKind_ = ElementKind::HoleyAny;
        ((int64_t*)elements)[index] = value;
        ts_gc_write_barrier(&((int64_t*)elements)[index], (void*)value);
        return;
    }
    // Far sparse region: spill to the overflow store (keyed by integer index).
    if (elementKind_ != ElementKind::HoleyAny) elementKind_ = ElementKind::HoleyAny;
    if (!sparseElements) {
        sparseElements = TsMap::Create();
        ts_gc_write_barrier(&this->sparseElements, sparseElements);
    }
    TsValue key((int64_t)index);
    TsValue v = nanbox_to_tagged((TsValue*)(uintptr_t)(uint64_t)value);
    sparseElements->Set(key, v);
}

int64_t TsArray::Get(size_t index) {
    if (index >= length) {
        // JavaScript behavior: return undefined (0) for out-of-bounds access
        return 0;
    }
    // Per-index accessor (Lever A): __arr_getter_<i>/__arr_setter_<i> in the
    // properties side-map governs reads at this index regardless of element kind
    // (HoleySmi/HoleyDouble don't store NANBOX_HOLE, so this must precede the
    // slot read). Gated on `properties` so the common no-side-map array pays only
    // a pointer-null check.
    if (properties) {
        if (TsValue* getter = array_index_accessor_fn(this, index, true)) {
            extern TsValue* ts_call_with_this_0(TsValue* boxedFunc, TsValue* thisArg);
            TsValue* res = ts_call_with_this_0(getter, ts_value_make_array(this));
            return res ? (int64_t)(uintptr_t)res
                       : (int64_t)(uintptr_t)ts_value_make_undefined();
        }
        if (array_index_accessor_fn(this, index, false)) {
            return (int64_t)(uintptr_t)ts_value_make_undefined();  // setter-only
        }
    }
    if (elementSize != 8) return 0;
    return readSlot(index);
}

void TsArray::Set(size_t index, int64_t value) {
    if (elementSize != 8) {
        // Specialized (non-8-byte) arrays stay dense; preserve legacy behavior.
        if (index < length) return;  // (legacy: silent no-op past length)
        return;
    }
    if (index < length) {
        // In-range: dense slot if index < capacity, else the sparse region of
        // a capped array. writeSlot handles the write barrier.
        writeSlot(index, value);
        return;
    }
    if (index == length && length < kMaxDenseElements) {
        Push(value);
        return;
    }
    // Extend with a gap: holes for [length, index), value at index. Route
    // through writeSlot so allocation stays bounded (sparse store beyond
    // kMaxDenseElements) instead of panicking or OOM-growing the dense buffer.
    size_t oldLen = length;
    length = index + 1;
    if (index > oldLen) has_holes_ = true;  // a gap [oldLen, index) is real holes
    if (elementKind_ != ElementKind::HoleyAny) elementKind_ = ElementKind::HoleyAny;
    // Hole-fill the part of the gap that lies in the current dense buffer;
    // slots beyond capacity are implicit holes (readSlot) or hole-filled by
    // writeSlot's bounded grow. Avoids exposing stale slot bytes as values.
    for (size_t i = oldLen; i < index && i < capacity; i++) {
        ((int64_t*)elements)[i] = (int64_t)NANBOX_HOLE;
    }
    writeSlot(index, value);
}

int64_t TsArray::Length() {
    return length;
}

bool TsArray::SetLength(size_t newLength) {
    if (newLength == length) return true;
    if (newLength < length) {
        // Truncate. Elements above newLength become unreachable; GC will
        // collect them. We just decrement length; the slots stay in the
        // backing buffer (capacity is unchanged) but iteration / Get /
        // hasOwnProperty all bound on length so they're invisible.
        length = newLength;
        // Drop any sparse-store entries at or beyond the new length.
        if (sparseElements) {
            // Cheap correctness: clear the whole store if everything is now
            // out of range; otherwise leave entries (reads bound on length so
            // stale high entries are invisible, and a later re-extend would
            // legitimately re-expose them only if they're < the new length).
            if (newLength <= capacity) {
                sparseElements = nullptr;
            }
        }
        return true;
    }
    // Extend. If the new length is beyond the dense-growth ceiling, become a
    // SPARSE array: set the logical length but DO NOT eager-allocate the
    // backing (that is the `a.length = 2.1e9` -> 17GB OOM). Indices in
    // [capacity, length) read as holes via readSlot; writes spill to the
    // sparse store. Force HoleyAny so getters take the NaN-box hole path.
    if (newLength > kMaxDenseElements) {
        elementKind_ = ElementKind::HoleyAny;
        // Hole-fill the dense slots that are now in logical range
        // [length, capacity); without this they'd read back as stale bytes
        // (the buffer from Create() isn't hole-initialized). Slots beyond
        // capacity are implicit holes via readSlot.
        if (elementSize == 8) {
            for (size_t i = length; i < capacity; i++) {
                ((int64_t*)elements)[i] = (int64_t)NANBOX_HOLE;
            }
        }
        length = newLength;
        return true;
    }
    // Extend with holes. Reallocate the backing buffer if needed; new
    // slots are NANBOX_HOLE so iteration (per IsHole()) skips them and
    // hasOwnProperty(arr, i) returns false for them.
    if (newLength > capacity) {
        size_t newCapacity = capacity ? capacity : 4;
        while (newCapacity < newLength) newCapacity *= 2;
        void* newElements = ts_alloc(newCapacity * elementSize);
        if (length > 0) {
            std::memcpy(newElements, elements, length * elementSize);
        }
        elements = newElements;
        ts_gc_write_barrier((void*)&this->elements, newElements);
        ts_gc_write_barrier_range(newElements, length * elementSize);
        capacity = newCapacity;
    }
    if (elementSize == 8) {
        if (newLength > length) has_holes_ = true;  // padded region [length,newLength) is holes
        for (size_t i = length; i < newLength; i++) {
            ((int64_t*)elements)[i] = (int64_t)NANBOX_HOLE;
        }
    }
    length = newLength;
    return true;
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

bool jsDefaultSortComparator(int64_t a, int64_t b) {
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

// Helper to detect TsTypedArray: magic at offset 16 (TsObject::magic)
static inline TsTypedArray* asTypedArray(void* ptr) {
    if (!ptr) return nullptr;
    // TsTypedArray extends TsObject, magic is at offset 16
    uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
    if (magic16 == TsTypedArray::MAGIC) return (TsTypedArray*)ptr;
    return nullptr;
}

// Unbox a potentially NaN-boxed pointer and return raw pointer
static inline void* unboxRaw(void* arr) {
    if (!arr) return nullptr;
    void* raw = ts_value_get_object((TsValue*)arr);
    return raw ? raw : arr;
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

    // Forward decls for non-Array receiver fallback (compiler Any-path
    // lowers obj.method() to these externs regardless of obj's real type;
    // guard magic and delegate to the spec-compliant native wrappers).
    extern TsValue* ts_array_push_native(void* ctx, int argc, TsValue** argv);
    extern TsValue* ts_array_concat_native(void* ctx, int argc, TsValue** argv);

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

        // Guard non-TsArray receivers (compiler Any-typed lowering bug
        // workaround; see ts_array_join for details).
        {
            uintptr_t p = (uintptr_t)rawArr;
            uint32_t magic = 0;
            if (p > 0x1000 && p < 0x0000800000000000ULL) {
                magic = *(uint32_t*)rawArr;
            }
            if (magic != TsArray::MAGIC) {
                // Non-Array receiver: the compiler may have mis-dispatched a
                // method call on a variable whose STATIC type was Array but
                // whose runtime value is a different object — e.g. lodash
                // baseUniq's `var seen = result; ...; seen = new SetCache();
                // seen.push(x)`. `seen.push` is statically bound to Array.push
                // (because `seen` was inferred Array), so it lands here. Prefer
                // the receiver's OWN `push` method so its side effects happen on
                // the real object; otherwise ts_array_push_native materializes a
                // throwaway temp array and the push is lost (uniq/uniqBy large-
                // array dedup silently failed). Mirrors the ts_array_join/concat
                // non-array workarounds above.
                extern TsValue* ts_object_get_property(void* obj, const char* key);
                TsValue* ownPush = ts_object_get_property(rawArr, "push");
                if (ownPush && !ts_value_is_undefined(ownPush)) {
                    extern TsValue* ts_call_with_this_1(TsValue* fn, TsValue* thisArg, TsValue* arg1);
                    ts_call_with_this_1(ownPush, (TsValue*)arr, (TsValue*)value);
                    return;
                }
                TsValue* argvBuf[1] = { (TsValue*)value };
                ts_array_push_native(arr, 1, argvBuf);
                return;
            }
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

    extern TsValue* ts_array_unshift_native(void* ctx, int argc, TsValue** argv);

    // Returns the array's new length. The HIRToLLVM lowering (unshift case)
    // declares this returning i64 and uses it as the result of `arr.unshift(x)`;
    // when this returned void the typed call read a garbage register (e.g.
    // `[1,2,3].unshift(0)` returned a huge bogus number instead of 4).
    int64_t ts_array_unshift(void* arr, void* value) {
        // Guard non-TsArray receivers (same lowering bug as join/push/concat;
        // see HIRToLLVM.cpp:6209). Plain TsMap / primitive receivers would
        // read garbage fields if cast to TsArray*.
        if (arr) {
            uintptr_t p = (uintptr_t)arr;
            uint32_t magic = 0;
            if (p > 0x1000 && p < 0x0000800000000000ULL) {
                magic = *(uint32_t*)arr;
            }
            if (magic != TsArray::MAGIC) {
                TsValue* argvBuf[1] = { (TsValue*)value };
                TsValue* r = ts_array_unshift_native(arr, 1, argvBuf);
                return r ? ts_value_get_int(r) : 0;
            }
        }
        if (!arr) return 0;
        ((TsArray*)arr)->Unshift((int64_t)value);
        return (int64_t)((TsArray*)arr)->Length();
    }

    void* ts_array_shift(void* arr) {
        return (void*)((TsArray*)arr)->Shift();
    }

    TsValue* ts_array_get_as_value(void* arr, int64_t index) {
        if (!arr) {
            return ts_value_make_undefined();
        }
        // Check for TsTypedArray (magic at offset 16)
        TsTypedArray* ta = asTypedArray(arr);
        if (ta) {
            if (index < 0 || (size_t)index >= ta->GetLength()) return ts_value_make_undefined();
            return ts_value_make_double(ta->Get((size_t)index));
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
        // Generic (PackedAny/HoleyAny) path: `val` is NaN-boxed, so NANBOX_HOLE
        // is unambiguous (unlike the SMI/Double fast paths). A hole reads as
        // undefined; otherwise the 0x08 sentinel leaks via ts_object_get_dynamic
        // (`array[i]` on an any-typed array) -> "unknown" / pointer crash.
        if ((uint64_t)val == NANBOX_HOLE) return ts_value_make_undefined();
        return (TsValue*)val;
    }

    // Value-based variant - returns TsValue by value to avoid heap allocation
    TsValue ts_array_get_v(void* arr, int64_t index) {
        TsValue result;
        result.type = ValueType::UNDEFINED;
        result.ptr_val = nullptr;

        if (!arr) return result;

        // Check for TsTypedArray (magic at offset 16)
        TsTypedArray* ta = asTypedArray(arr);
        if (ta) {
            if (index < 0 || (size_t)index >= ta->GetLength()) return result;
            result.type = ValueType::NUMBER_DBL;
            result.d_val = ta->Get((size_t)index);
            return result;
        }

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

        // Check for TsTypedArray (magic at offset 16)
        TsTypedArray* ta = asTypedArray(arr);
        if (ta) {
            if (index < 0 || (size_t)index >= ta->GetLength()) return;
            double dval = 0;
            if (value.type == ValueType::NUMBER_DBL) dval = value.d_val;
            else if (value.type == ValueType::NUMBER_INT) dval = (double)value.i_val;
            ta->Set((size_t)index, dval);
            return;
        }

        // If the receiver is not a TsArray, forward to the generic object
        // property setter — `obj[0] = v` on a plain object or TsMap/TsFlatObject
        // should install a string-keyed property, not silently no-op.
        // Guard against NaN-boxed primitives / non-pointers: require arr to
        // look like a heap pointer (>4K, <48-bit canonical).
        {
            uintptr_t p = (uintptr_t)arr;
            bool looksPtr = p > 0x1000 && p < 0x0000800000000000ULL;
            if (looksPtr) {
                uint32_t m = *(uint32_t*)arr;
                if (m != TsArray::MAGIC) {
                    TsValue* keyBoxed = ts_value_make_int(index);
                    TsValue* valBoxed = nanbox_from_tagged(value);
                    ts_object_set_property(arr, keyBoxed, valBoxed);
                    return;
                }
            }
        }

        TsArray* array = (TsArray*)arr;

        // ECMA-262 array-index range guard. Indices outside [0, 2^32-2]
        // (including negative values, NaN/Infinity lowered to poison i64,
        // and out-of-range numeric literals) must be installed as
        // string-keyed properties — not as array elements — to avoid
        // unbounded growth and OOM crashes. We pass the index as a STRING
        // key so ts_object_set_prop_v doesn't take its numeric-key
        // fast-path back into us (would recurse → stack overflow).
        constexpr int64_t kMaxArrayIndex = 0xFFFFFFFELL;
        if (index < 0 || index > kMaxArrayIndex) {
            void* keyStr = ts_int_to_string(index, 10);
            TsValue* keyBoxed = ts_value_make_string(keyStr);
            TsValue* valBoxed = nanbox_from_tagged(value);
            ts_object_set_property(arr, keyBoxed, valBoxed);
            return;
        }

        // A4: intercept a write to an accessor / non-writable array index
        // before the element store.
        if (array->properties &&
            array_index_write_intercept(array, (size_t)index, nanbox_from_tagged(value)))
            return;

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
        void* raw = unboxRaw(arr);
        if (!raw) return (void*)ts_value_make_undefined();

        // Check for TsTypedArray. After detach, GetLength returns 0 so
        // any access falls into the out-of-range branch; return undefined
        // (per spec — IntegerIndexedElementGet on detached returns
        // undefined, not throws).
        TsTypedArray* ta = asTypedArray(raw);
        if (ta) {
            if (index < 0 || (size_t)index >= ta->GetLength()) {
                return (void*)ts_value_make_undefined();
            }
            double val = ta->Get((size_t)index);
            // Return as NaN-boxed double
            return (void*)ts_value_make_double(val);
        }

        // If `raw` is not actually a TsArray ("ARRY" magic at offset 0) -- e.g. a
        // class constructor / function / plain object reached via a numeric index
        // like `C[1.1]` -- do NOT read TsArray fields off it (that crashed in
        // readSlot). Route the numeric key to ordinary property access.
        if ((uintptr_t)raw < 0x1000 || *(uint32_t*)raw != 0x41525259) {  // not "ARRY"
            if (index < 0) return (void*)ts_value_make_undefined();
            void* keyStr = ts_int_to_string(index, 10);
            const char* keyC = ((TsString*)keyStr)->ToUtf8();
            return (void*)ts_object_get_property(raw, keyC);
        }

        TsArray* array = (TsArray*)raw;
        constexpr int64_t kMaxArrayIndex = 0xFFFFFFFELL;
        if (index < 0 || index > kMaxArrayIndex) {
            // Negative / out-of-range numeric keys are NOT array elements —
            // the SET path (ts_array_set, ~line 2122) installs them in the
            // string-keyed `properties` side map. Mirror that here so
            // `a[-1]=v; a[-1]` reads back `v` instead of undefined. Use a
            // STRING key so the property getter doesn't re-enter a numeric
            // array fast path. (Indices in [length, kMaxArrayIndex] remain
            // element reads → hole → undefined, matching the set side.)
            void* keyStr = ts_int_to_string(index, 10);
            const char* keyC = ((TsString*)keyStr)->ToUtf8();
            return (void*)ts_object_get_property(raw, keyC);
        }
        if ((size_t)index >= (size_t)array->Length()) {
            return (void*)ts_value_make_undefined();
        }
        // Get() is accessor-aware (invokes a per-index getter defined via
        // Object.defineProperty); for a plain array it is the slot read.
        int64_t slot = array->Get((size_t)index);
        // ECMA-262 §10.4.2.1 [[Get]] on a hole walks the prototype chain: an
        // inherited index (e.g. `Array.prototype[1]=x`) supplies the value the
        // missing own slot would have. array_proto_get_at consults
        // Array.prototype (data property or getter); a plain Array.prototype
        // yields nullptr → undefined, preserving `a[1] === undefined` for sparse
        // arrays. Iteration that must distinguish holes uses TsArray::IsHole().
        if ((uint64_t)slot == NANBOX_HOLE) {
            TsValue* inh = array_proto_get_at((void*)array, index);
            return inh ? (void*)inh : (void*)ts_value_make_undefined();
        }
        return (void*)slot;
    }

    void ts_array_set_unchecked(void* arr, int64_t index, void* value) {
        void* raw = unboxRaw(arr);
        if (!raw) return;

        // Check for TsTypedArray
        TsTypedArray* ta = asTypedArray(raw);
        if (ta) {
            if (index < 0 || (size_t)index >= ta->GetLength()) return;
            // Decode value to double
            TsValue decoded = nanbox_to_tagged((TsValue*)value);
            double dval = 0;
            if (decoded.type == ValueType::NUMBER_DBL) dval = decoded.d_val;
            else if (decoded.type == ValueType::NUMBER_INT) dval = (double)decoded.i_val;
            ta->Set((size_t)index, dval);
            return;
        }

        // Fall through to generic object-property set when receiver is not
        // a TsArray — `obj[0] = v` on a plain object / TsMap / TsFlatObject
        // should install a string-keyed property, not silently no-op.
        {
            uintptr_t p = (uintptr_t)raw;
            if (p > 0x1000 && p < 0x0000800000000000ULL) {
                uint32_t m = *(uint32_t*)raw;
                if (m != TsArray::MAGIC) {
                    TsValue* keyBoxed = ts_value_make_int(index);
                    ts_object_set_property(raw, keyBoxed, value);
                    return;
                }
            }
        }

        TsArray* array = (TsArray*)raw;

        // ECMA-262: A property name P is an array index iff
        // ToString(ToUint32(P)) === P AND ToUint32(P) !== 2^32-1.
        // Anything outside [0, 2^32-2] must be installed as a string-keyed
        // property, not as an array element. Without this guard, indices
        // like NaN/Infinity (lowered to poison i64 by FPToSI) or huge
        // numeric literals trigger a runaway growth loop that OOMs.
        // String key avoids recursion through ts_object_set_prop_v's
        // numeric-key fast-path back into ts_array_set_v.
        constexpr int64_t kMaxArrayIndex = 0xFFFFFFFELL;  // 2^32 - 2
        if (index < 0 || index > kMaxArrayIndex) {
            // Negative and out-of-range numeric keys are NOT array elements:
            // install them in the string-keyed `properties` side map (the
            // get side mirrors this). Previously `if (index < 0) return;`
            // silently DROPPED negative writes (`a[-1]=v` was lost). Casting
            // a negative index to size_t below would also force a runaway
            // grow→OOM, so this guard must precede the element path.
            void* keyStr = ts_int_to_string(index, 10);
            TsValue* keyBoxed = ts_value_make_string(keyStr);
            ts_object_set_property(raw, keyBoxed, value);
            return;
        }

        size_t idx = (size_t)index;
        size_t len = (size_t)array->Length();

        // Both in-range and gap-extend go through Set, which fills the gap
        // [len, idx) with holes and bounds allocation (sparse store beyond
        // kMaxDenseElements). The previous Push-until-idx loop OOM'd for a
        // large idx (e.g. `a.length=1e9; a[5e8]=x`).
        (void)len;
        if (array->properties &&
            array_index_write_intercept(array, idx, value)) return;
        array->Set(idx, (int64_t)value);
    }

    int64_t ts_array_length(void* arr) {
        if (!arr) return 0;

        // With NaN boxing, arr may be a NaN-boxed pointer
        // Use ts_value_get_object to decode
        void* raw = ts_value_get_object((TsValue*)arr);
        if (raw) arr = raw;
        else if ((uint64_t)(uintptr_t)arr < 0x10000) return 0;  // NaN-boxed special (undefined=0x0A, null=0x02, etc.)

        // Check for TsTypedArray (magic at offset 16)
        TsTypedArray* ta = asTypedArray(arr);
        if (ta) return (int64_t)ta->GetLength();

        // Check magic to handle TsRegExpMatchArray
        uint32_t magic = *(uint32_t*)arr;
        if (magic == 0x524D4154) { // TsRegExpMatchArray::MAGIC ("RMAT")
            return ((TsRegExpMatchArray*)arr)->Length();
        }
        return ((TsArray*)arr)->Length();
    }

    bool ts_array_isArray(void* value) {
        if (!value) return false;

        // An `arguments` object is array-like but NOT an Array (Array.isArray
        // returns false). See ts_array_is_array for the mirror.
        auto isArgs = [](void* p) -> bool {
            return p && *(uint32_t*)p == TsArray::MAGIC && ((TsArray*)p)->isArguments;
        };
        // Decode NaN-boxed value
        TsValue decoded = nanbox_to_tagged((TsValue*)value);
        if (decoded.type == ValueType::ARRAY_PTR) {
            return !isArgs(decoded.ptr_val);
        }
        if (decoded.type == ValueType::OBJECT_PTR && decoded.ptr_val) {
            uint32_t magic = *(uint32_t*)decoded.ptr_val;
            if (magic == TsArray::MAGIC) return !isArgs(decoded.ptr_val);
            return magic == 0x524D4154; // TsRegExpMatchArray
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
            result = tsCall((TsValue*)g_current_comparator, aVal, bVal);
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
            result = tsCall((TsValue*)g_current_comparator, aVal, bVal);
        }
        if (!result) return a < b;

        // Get the result as an int (negative = a < b, zero = equal, positive = a > b)
        int64_t cmp = ts_value_get_int(result);
        return cmp < 0;
    }

    extern TsValue* ts_array_sort_native(void* ctx, int argc, TsValue** argv);

    // ts_array_sort - HIR calls this with (array, comparator) and expects array to be returned
    void* ts_array_sort(void* arr, void* comparator) {
        void* rawArr = ts_nanbox_safe_unbox(arr);
        if (rawArr) {
            uintptr_t p = (uintptr_t)rawArr;
            if (p > 0x1000 && p < 0x0000800000000000ULL) {
                uint32_t magic = *(uint32_t*)rawArr;
                if (magic != TsArray::MAGIC) {
                    TsValue* argvBuf[1] = { (TsValue*)comparator };
                    TsValue* res = ts_array_sort_native(arr, 1, argvBuf);
                    return res ? (void*)ts_value_get_object(res) : nullptr;
                }
            }
        }
        TsArray* array = (TsArray*)rawArr;

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

    extern "C" void* ts_array_species_rematerialize(void* receiver, void* resultRaw);  // fwd (defined below)

    void* ts_array_slice(void* arr, int64_t start, int64_t end) {
        if (!arr) return ts_array_create();
        TsTypedArray* ta = asTypedArray(arr);
        if (ta) {
            int64_t len = (int64_t)ta->GetLength();
            if (start < 0) start = std::max<int64_t>(len + start, 0);
            if (end < 0) end = std::max<int64_t>(len + end, 0);
            if (start > len) start = len;
            if (end > len) end = len;
            if (end <= start) {
                return TsTypedArray::Create(0, ta->GetElementSize(), false, ta->GetType());
            }
            size_t count = (size_t)(end - start);
            TsTypedArray* result = TsTypedArray::Create(count, ta->GetElementSize(), false, ta->GetType());
            uint8_t* srcData = ta->GetData() + (size_t)start * ta->GetElementSize();
            memcpy(result->GetData(), srcData, count * ta->GetElementSize());
            return result;
        }
        void* result = ((TsArray*)arr)->Slice(start, end);
        return ts_array_species_rematerialize(arr, result);  // ECMA-262 ArraySpeciesCreate
    }

    // Small helper: if receiver is a TsTypedArray, return it; else nullptr.
    // Every use site is a TypedArray prototype-method implementation, so
    // this doubles as ECMA-262 ValidateTypedArray: a fixed-extent view left
    // out of bounds by a resizable-buffer shrink throws TypeError here
    // (the 74-test "implements ArrayBuffer.prototype.resize" family).
    static TsTypedArray* try_as_typed_array(void* arr) {
        if (!arr) return nullptr;
        void* raw = ts_nanbox_safe_unbox(arr);
        if (!raw) raw = arr;
        uintptr_t p = (uintptr_t)raw;
        if (p <= 0x1000 || p >= 0x0000800000000000ULL) return nullptr;
        uint32_t m16 = *(uint32_t*)((char*)raw + 16);
        if (m16 != TsTypedArray::MAGIC) return nullptr;
        TsTypedArray* ta = (TsTypedArray*)raw;
        if (ta->IsOutOfBounds()) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "TypedArray is out of bounds on its ArrayBuffer"));
            return nullptr;  // unreachable
        }
        return ta;
    }

    int64_t ts_array_indexOf(void* arr, int64_t value) {
        if (TsTypedArray* ta = try_as_typed_array(arr)) {
            // `value` is the raw bit pattern of a boxed TsValue* or small int.
            // Decode to a double for element comparison.
            uint64_t nb = (uint64_t)value;
            double target;
            if (nanbox_is_number(nb)) target = nanbox_to_number(nb);
            else target = ts_to_number((TsValue*)value);  // throws TypeError on Symbol
            int64_t len = (int64_t)ta->GetLength();
            for (int64_t i = 0; i < len; i++) {
                if (ta->Get((size_t)i) == target) return i;
            }
            return -1;
        }
        return ((TsArray*)arr)->IndexOf(value);
    }

    int64_t ts_array_indexOf_from(void* arr, int64_t value, double fromIndex) {
        TsArray* a = (TsArray*)arr;
        int64_t len = a->Length();
        // Spec: NaN -> 0; +Infinity -> miss; -Infinity -> 0.
        int64_t fi;
        if (fromIndex != fromIndex) fi = 0; // NaN
        else if (std::isinf(fromIndex)) { if (fromIndex > 0) return -1; fi = 0; }
        else {
            fi = (int64_t)fromIndex;
            if (fi < 0) fi = len + fi;
            if (fi < 0) fi = 0;
        }
        if (fi >= len) return -1;
        return a->IndexOf(value, (size_t)fi);
    }

    int64_t ts_array_lastIndexOf(void* arr, int64_t value) {
        if (TsTypedArray* ta = try_as_typed_array(arr)) {
            uint64_t nb = (uint64_t)value;
            double target;
            if (nanbox_is_number(nb)) target = nanbox_to_number(nb);
            else target = ts_to_number((TsValue*)value);  // throws TypeError on Symbol
            int64_t len = (int64_t)ta->GetLength();
            for (int64_t i = len - 1; i >= 0; i--) {
                if (ta->Get((size_t)i) == target) return i;
            }
            return -1;
        }
        return ((TsArray*)arr)->LastIndexOf(value);
    }

    int64_t ts_array_lastIndexOf_from(void* arr, int64_t value, double fromIndex) {
        TsArray* a = (TsArray*)arr;
        int64_t len = a->Length();
        if (len == 0) return -1;
        // Spec: NaN -> 0; +Infinity -> len-1; -Infinity -> miss.
        int64_t fi;
        if (fromIndex != fromIndex) fi = 0; // NaN
        else if (std::isinf(fromIndex)) { if (fromIndex < 0) return -1; fi = len - 1; }
        else {
            fi = (int64_t)fromIndex;
            if (fi < 0) fi = len + fi;
            if (fi < 0) return -1;
            if (fi >= len) fi = len - 1;
        }
        return a->LastIndexOf(value, fi);
    }

    void* ts_array_flat(void* arr, int64_t depth) {
        // ECMA-262 22.1.3.10: `arr.flat()` with no depth argument defaults to 1.
        // HIRToLLVM passes INT64_MIN as the "missing arg" sentinel for variadic
        // builtin lowerings, so convert that back to the spec default here.
        if (depth == INT64_MIN) depth = 1;
        return ((TsArray*)arr)->Flat(depth);
    }

    // Forward decl — defined further down. Used by the C-level array
    // method entry points to throw TypeError for non-callable callbacks
    // per ECMA-262 spec.
    static bool array_require_callable(void* callback, const char* name);

    void* ts_array_flatMap(void* arr, void* callback, void* thisArg) {
        if (!array_require_callable(callback, "flatMap")) return nullptr;
        return ((TsArray*)arr)->FlatMap(callback, thisArg);
    }

    bool ts_array_includes(void* arr, int64_t value) {
        if (TsTypedArray* ta = try_as_typed_array(arr)) {
            uint64_t nb = (uint64_t)value;
            double target;
            if (nanbox_is_number(nb)) target = nanbox_to_number(nb);
            else target = ts_to_number((TsValue*)value);  // throws TypeError on Symbol
            bool targetNaN = (target != target);
            int64_t len = (int64_t)ta->GetLength();
            for (int64_t i = 0; i < len; i++) {
                double v = ta->Get((size_t)i);
                if (targetNaN) { if (v != v) return true; }
                else if (v == target) return true;
            }
            return false;
        }
        return ((TsArray*)arr)->Includes(value);
    }

    bool ts_array_includes_from(void* arr, int64_t value, double fromIndex) {
        TsArray* a = (TsArray*)arr;
        int64_t len = a->Length();
        int64_t fi;
        if (fromIndex != fromIndex) fi = 0; // NaN
        else if (std::isinf(fromIndex)) { if (fromIndex > 0) return false; fi = 0; }
        else {
            fi = (int64_t)fromIndex;
            if (fi < 0) fi = len + fi;
            if (fi < 0) fi = 0;
        }
        if (fi >= len) return false;
        return a->Includes(value, (size_t)fi);
    }

    void* ts_array_at(void* arr, int64_t index) {
        if (!arr) return ts_value_make_undefined();
        // Unbox if the receiver is a boxed TsValue*
        void* raw = ts_nanbox_safe_unbox(arr);
        if (!raw) raw = arr;
        // Guard: TypedArray receivers are also compiled as .at() calls, but
        // TsTypedArray is not a TsArray. Delegate to its Get() + negative-index
        // semantics. try_as_typed_array doubles as ValidateTypedArray
        // (TypeError on an out-of-bounds view over a shrunk resizable buffer).
        if (TsTypedArray* ta = try_as_typed_array(raw)) {
            int64_t len = (int64_t)ta->GetLength();
            if (index < 0) index = len + index;
            if (index < 0 || index >= len) return ts_value_make_undefined();
            return ts_value_make_double(ta->Get((size_t)index));
        }
        TsArray* a = (TsArray*)raw;
        int64_t len = a->Length();
        // Handle negative indices
        if (index < 0) index = len + index;
        if (index < 0 || index >= len) {
            return ts_value_make_undefined();
        }
        // Return a properly boxed value based on array type
        return a->GetElementBoxed(index);
    }

    // Compiler fast-path entry for `arr.at(index)`: coerces the still-boxed
    // index via ToIntegerOrInfinity (throws TypeError on a Symbol/BigInt/
    // throwing-valueOf index) BEFORE delegating to ts_array_at. The typed
    // lowering boxes the index and calls this instead of doing a bare i64 cast.
    void* ts_array_at_coerced(void* arr, TsValue* index) {
        int64_t i = ts_to_index_integer(index);  // may throw
        return ts_array_at(arr, i);
    }

    // Compiler fast-path entries for fill/copyWithin: coerce the still-boxed
    // index/count arguments via ToIntegerOrInfinity (throws TypeError on a
    // Symbol/BigInt/throwing-valueOf argument) BEFORE delegating to the bare
    // i64 implementation. An omitted optional arg arrives as null and maps to
    // the INT64_MIN "not provided" sentinel the implementations already honor.
    // The fill VALUE argument is left boxed (it is not an index).
    void* ts_array_fill_coerced(void* arr, void* value, TsValue* start, TsValue* end) {
        int64_t s = ts_to_index_integer_or_sentinel(start);  // may throw
        int64_t e = ts_to_index_integer_or_sentinel(end);    // may throw
        return ts_array_fill(arr, value, s, e);
    }
    void* ts_array_copyWithin_coerced(void* arr, TsValue* target,
                                      TsValue* start, TsValue* end) {
        int64_t t = ts_to_index_integer_or_sentinel(target);  // may throw
        int64_t s = ts_to_index_integer_or_sentinel(start);   // may throw
        int64_t e = ts_to_index_integer_or_sentinel(end);     // may throw
        return ts_array_copyWithin(arr, t, s, e);
    }

    // Compiler fast-path entries for indexOf/lastIndexOf with a fromIndex:
    // coerce the still-boxed fromIndex via ToNumber (throws on Symbol/BigInt)
    // while preserving +/-Infinity. The search VALUE is left as the raw bit
    // pattern (its own comparison path handles coercion).
    int64_t ts_array_indexOf_from_coerced(void* arr, int64_t value, TsValue* fromIndex) {
        double fi = ts_to_index_number_or(fromIndex, 0.0);  // may throw
        return ts_array_indexOf_from(arr, value, fi);
    }
    int64_t ts_array_lastIndexOf_from_coerced(void* arr, int64_t value, TsValue* fromIndex) {
        double fi = ts_to_index_number_or(fromIndex, 0.0);  // may throw
        return ts_array_lastIndexOf_from(arr, value, fi);
    }

    // Forward decl for non-TsArray receiver fallback (plain objects,
    // primitives, etc. — Array.prototype.join.call(x) semantics).
    extern TsValue* ts_array_join_native(void* ctx, int argc, TsValue** argv);

    void* ts_array_join(void* arr, void* separator) {
        // Unbox if arr is a TsValue* (boxed array)
        void* rawArr = ts_nanbox_safe_unbox(arr);
        if (!rawArr) return TsString::Create("");
        // Per ES spec: if separator is undefined, use default ",".
        // Guard against boxed/raw undefined sentinels and NaN-boxed undefined.
        if (separator) {
            uint64_t sepBits = (uint64_t)separator;
            if (sepBits == 10 /* raw undefined sentinel */ ||
                nanbox_is_undefined(sepBits) || nanbox_is_null(sepBits)) {
                separator = nullptr;
            }
        }
        // TypedArray receiver: format elements as numbers.
        if (TsTypedArray* ta = try_as_typed_array(rawArr)) {
            std::string sep = ",";
            if (separator) {
                TsString* sepStr = (TsString*)ts_value_get_string((TsValue*)separator);
                if (!sepStr) sepStr = (TsString*)separator;
                if (sepStr) {
                    const char* u = sepStr->ToUtf8();
                    if (u) sep = u;
                }
            }
            std::string out;
            char buf[64];
            size_t len = ta->GetLength();
            for (size_t i = 0; i < len; i++) {
                if (i > 0) out += sep;
                double v = ta->Get(i);
                if (v != v) out += "NaN";
                else if (v == (int64_t)v && std::abs(v) < 1e16) {
                    snprintf(buf, sizeof(buf), "%lld", (long long)v);
                    out += buf;
                } else {
                    snprintf(buf, sizeof(buf), "%g", v);
                    out += buf;
                }
            }
            return TsString::Create(out.c_str());
        }
        // Guard: only a real TsArray is safe to cast. The compiler's
        // Any-typed method-call fast path lowers `obj.join()` to this
        // extern regardless of obj's actual type (HIRToLLVM.cpp:6152).
        // For non-Array receivers (plain TsMap, primitive, TsString),
        // delegate to the native wrapper which routes through
        // require_array_or_throw for spec-compliant array-like handling.
        {
            uintptr_t p = (uintptr_t)rawArr;
            uint32_t magic = 0;
            if (p > 0x1000 && p < 0x0000800000000000ULL) {
                magic = *(uint32_t*)rawArr;
            }
            if (magic != TsArray::MAGIC) {
                // Non-Array receiver: prefer the receiver's own `join` property
                // (lodash defines `lodash.join` and expects `_.join(arr, sep)`
                // to dispatch to its wrapped function, not to Array.prototype.join).
                extern TsValue* ts_object_get_property(void* obj, const char* key);
                TsValue* joinProp = ts_object_get_property(rawArr, "join");
                if (joinProp && !ts_value_is_undefined(joinProp)) {
                    extern TsValue* ts_call_with_this_1(TsValue* fn, TsValue* thisArg, TsValue* arg1);
                    TsValue* res = ts_call_with_this_1(joinProp, (TsValue*)arr, (TsValue*)separator);
                    if (!res) return TsString::Create("");
                    void* s = ts_value_get_string(res);
                    return s ? s : (void*)TsString::Create("");
                }
                TsValue* argvBuf[1] = { (TsValue*)separator };
                int argc = separator ? 1 : 0;
                TsValue* res = ts_array_join_native(arr, argc, argvBuf);
                if (!res) return TsString::Create("");
                // res is a boxed string; unwrap to raw TsString*.
                void* s = ts_value_get_string(res);
                return s ? s : (void*)TsString::Create("");
            }
        }
        return ((TsArray*)rawArr)->Join(separator);
    }

    void* ts_array_reverse(void* arr) {
        if (TsTypedArray* ta = try_as_typed_array(arr)) {
            size_t len = ta->GetLength();
            for (size_t i = 0, j = (len == 0 ? 0 : len - 1); i < j; i++, j--) {
                double a = ta->Get(i), b = ta->Get(j);
                ta->Set(i, b);
                ta->Set(j, a);
            }
            return arr;
        }
        ((TsArray*)arr)->Reverse();
        return arr;
    }

    extern TsValue* ts_array_splice_native(void* ctx, int argc, TsValue** argv);

    void* ts_array_splice(void* arr, int64_t start, int64_t deleteCount, void* items) {
        if (!arr) return ts_array_create();
        // Guard non-TsArray receivers (compiler Any-path lowering bug;
        // see HIRToLLVM.cpp:6285). Delegate to native wrapper which
        // correctly handles array-like via require_array_or_throw.
        {
            uintptr_t p = (uintptr_t)arr;
            uint32_t magic = 0;
            if (p > 0x1000 && p < 0x0000800000000000ULL) {
                magic = *(uint32_t*)arr;
            }
            if (magic != TsArray::MAGIC) {
                // splice native takes argv = [start, deleteCount, ...items].
                // `items` here is already packed as a TsArray by the compiler
                // (see ArrayHandler::lowerArraySplice). Extract and forward.
                TsValue* startV = ts_value_make_int(start);
                TsValue* countV = ts_value_make_int(deleteCount);
                std::vector<TsValue*> argvBuf;
                argvBuf.push_back(startV);
                argvBuf.push_back(countV);
                if (items) {
                    TsArray* itemsArr = (TsArray*)items;
                    // Verify items is actually a TsArray before iterating
                    uint32_t im = *(uint32_t*)items;
                    if (im == TsArray::MAGIC) {
                        for (size_t i = 0; i < (size_t)itemsArr->Length(); ++i) {
                            argvBuf.push_back(itemsArr->GetElementBoxed(i));
                        }
                    }
                }
                TsValue* res = ts_array_splice_native(arr, (int)argvBuf.size(),
                                                     argvBuf.data());
                return res ? (void*)ts_value_get_object(res) : (void*)ts_array_create();
            }
        }
        TsArray* a = (TsArray*)arr;
        int64_t len = a->Length();

        // Normalize start
        if (start < 0) start = len + start;
        if (start < 0) start = 0;
        if (start > len) start = len;

        // Normalize deleteCount
        if (deleteCount < 0) deleteCount = 0;
        if (deleteCount > len - start) deleteCount = len - start;

        // Collect deleted elements into result
        TsArray* result = TsArray::Create(deleteCount > 0 ? deleteCount : 4);
        for (int64_t i = 0; i < deleteCount; i++) {
            result->Push(a->Get(start + i));
        }

        // Collect tail (elements after splice region)
        TsArray* tail = TsArray::Create(4);
        for (int64_t i = start + deleteCount; i < len; i++) {
            tail->Push(a->Get(i));
        }

        // Truncate to start
        while (a->Length() > start) {
            a->Pop();
        }

        // Insert new items (items is a TsArray* or null)
        if (items) {
            TsArray* itemsArr = (TsArray*)items;
            for (int64_t i = 0; i < itemsArr->Length(); i++) {
                a->Push(itemsArr->Get(i));
            }
        }

        // Re-add tail
        for (int64_t i = 0; i < tail->Length(); i++) {
            a->Push(tail->Get(i));
        }

        // ECMA-262 23.1.3.31: the returned removed-elements array is built via
        // ArraySpeciesCreate(O, actualDeleteCount).
        return ts_array_species_rematerialize(arr, result);
    }

    extern TsValue* ts_array_toReversed_native(void* ctx, int argc, TsValue** argv);
    extern TsValue* ts_array_toSorted_native(void* ctx, int argc, TsValue** argv);
    extern TsValue* ts_array_toSpliced_native(void* ctx, int argc, TsValue** argv);

    // Helper: true when `arr` is safely dereferenceable and has TsArray magic.
    static inline bool arr_is_tsarray(void* rawArr) {
        if (!rawArr) return false;
        uintptr_t p = (uintptr_t)rawArr;
        if (p <= 0x1000 || p >= 0x0000800000000000ULL) return false;
        return *(uint32_t*)rawArr == TsArray::MAGIC;
    }

    // ES2023 "change array by copy" methods
    void* ts_array_toReversed(void* arr) {
        void* rawArr = ts_nanbox_safe_unbox(arr);
        if (!arr_is_tsarray(rawArr)) {
            TsValue* res = ts_array_toReversed_native(arr, 0, nullptr);
            return res ? (void*)ts_value_get_object(res) : (void*)ts_array_create();
        }
        return ((TsArray*)rawArr)->ToReversed();
    }

    void* ts_array_toSorted(void* arr) {
        void* rawArr = ts_nanbox_safe_unbox(arr);
        if (!arr_is_tsarray(rawArr)) {
            TsValue* res = ts_array_toSorted_native(arr, 0, nullptr);
            return res ? (void*)ts_value_get_object(res) : (void*)ts_array_create();
        }
        return ((TsArray*)rawArr)->ToSorted();
    }

    void* ts_array_toSpliced(void* arr, int64_t start, int64_t deleteCount, void* items, int64_t itemCount) {
        void* rawArr = ts_nanbox_safe_unbox(arr);
        if (!arr_is_tsarray(rawArr)) {
            // Pack (start, deleteCount, ...items) for the native signature.
            // items is a TsArray* of spread elements; we rebuild argv inline.
            TsArray* itemsArr = (TsArray*)items;
            int itemsN = (itemsArr && arr_is_tsarray(itemsArr)) ? (int)itemsArr->Length() : 0;
            int argc = 2 + itemsN;
            std::vector<TsValue*> argvBuf;
            argvBuf.reserve(argc);
            argvBuf.push_back(ts_value_make_int(start));
            argvBuf.push_back(ts_value_make_int(deleteCount));
            for (int i = 0; i < itemsN; i++) {
                argvBuf.push_back((TsValue*)itemsArr->Get(i));
            }
            TsValue* res = ts_array_toSpliced_native(arr, argc, argvBuf.data());
            return res ? (void*)ts_value_get_object(res) : (void*)ts_array_create();
        }
        // items is a TsArray*, we need to pass it as-is so ToSpliced can properly extract values
        return ((TsArray*)rawArr)->ToSpliced(start, deleteCount, items, itemCount);
    }

    void* ts_array_with(void* arr, int64_t index, void* value) {
        return ((TsArray*)arr)->With(index, (int64_t)value);
    }

    // Forward decls for delegating TypedArray receivers to native wrappers
    // that route through require_array_or_throw (array-like materialization).
    extern TsValue* ts_array_forEach_native(void* ctx, int argc, TsValue** argv);
    extern TsValue* ts_array_map_native(void* ctx, int argc, TsValue** argv);
    extern TsValue* ts_array_filter_native(void* ctx, int argc, TsValue** argv);

    void ts_array_forEach(void* arr, void* callback, void* thisArg) {
        if (try_as_typed_array(arr)) {
            TsValue* argvBuf[2] = { (TsValue*)callback, (TsValue*)thisArg };
            ts_array_forEach_native(arr, 2, argvBuf);
            return;
        }
        if (!array_require_callable(callback, "forEach")) return;
        ((TsArray*)arr)->ForEach(callback, thisArg);
    }

    // Get the per-class TypedArray constructor matching a TsTypedArray's
    // element type (the spec's "default constructor" for SpeciesConstructor).
    extern "C" {
        void* ts_get_global_Int8Array();
        void* ts_get_global_Uint8Array();
        void* ts_get_global_Uint8ClampedArray();
        void* ts_get_global_Int16Array();
        void* ts_get_global_Uint16Array();
        void* ts_get_global_Int32Array();
        void* ts_get_global_Uint32Array();
        void* ts_get_global_Float32Array();
        void* ts_get_global_Float64Array();
        void* ts_get_global_BigInt64Array();
        void* ts_get_global_BigUint64Array();
        void* ts_get_global_Array();
    }
    static void* default_ta_ctor_for(TsTypedArray* ta) {
        if (!ta) return nullptr;
        switch (ta->GetType()) {
            case TypedArrayType::Int8:    return ts_get_global_Int8Array();
            case TypedArrayType::Uint8:   return ts_get_global_Uint8Array();
            case TypedArrayType::Uint8Clamped: return ts_get_global_Uint8ClampedArray();
            case TypedArrayType::Int16:   return ts_get_global_Int16Array();
            case TypedArrayType::Uint16:  return ts_get_global_Uint16Array();
            case TypedArrayType::Int32:   return ts_get_global_Int32Array();
            case TypedArrayType::Uint32:  return ts_get_global_Uint32Array();
            case TypedArrayType::Float32: return ts_get_global_Float32Array();
            case TypedArrayType::Float64: return ts_get_global_Float64Array();
            case TypedArrayType::BigInt64:  return ts_get_global_BigInt64Array();
            case TypedArrayType::BigUint64: return ts_get_global_BigUint64Array();
            default: return nullptr;
        }
    }

    // SpeciesConstructor(O, defaultCtor) per ECMA-262 7.3.20:
    //   1. Let C = Get(O, "constructor"). If undefined, return defaultCtor.
    //   2. If Type(C) is not Object, throw TypeError.
    //   3. Let S = Get(C, @@species). If undefined or null, return defaultCtor.
    //   4. If IsConstructor(S), return S; else throw TypeError.
    // Returns a TsValue* boxing the constructor, or null on TypeError-throw.
    static TsValue* species_constructor(void* exemplar, void* defaultCtor) {
        if (!exemplar || !defaultCtor) return (TsValue*)defaultCtor;
        TsValue* ctorVal = ts_object_get_property(exemplar, "constructor");
        if (!ctorVal || ts_value_is_undefined(ctorVal)) return (TsValue*)defaultCtor;
        // Step 2: ToObject check — for primitives, throw.
        uint64_t nb = nanbox_from_tsvalue_ptr(ctorVal);
        if (nanbox_is_int32(nb) || nanbox_is_double(nb) ||
            nanbox_is_true(nb)  || nanbox_is_false(nb) ||
            nanbox_is_null(nb)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "SpeciesConstructor: 'constructor' is not an Object"));
            return nullptr;
        }
        void* ctorRaw = ts_value_get_object(ctorVal);
        if (!ctorRaw) return (TsValue*)defaultCtor;
        // Get(C, @@species)
        TsValue* speciesVal = ts_object_get_property(ctorRaw, "[Symbol.species]");
        if (!speciesVal || ts_value_is_undefined(speciesVal) ||
            ts_value_is_null(speciesVal)) {
            return (TsValue*)defaultCtor;
        }
        // IsConstructor check: must be a TsFunction.
        void* spRaw = ts_value_get_object(speciesVal);
        if (!spRaw) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "SpeciesConstructor: @@species is not a constructor"));
            return nullptr;
        }
        // TsFunction has magic 0x46554E43 ("FUNC") at offset 16, or
        // TsClosure has 0x434C5352 ("RSCL") at offset 16.
        uint32_t m16 = *(uint32_t*)((char*)spRaw + 16);
        if (m16 != 0x46554E43 && m16 != 0x434C5352) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "SpeciesConstructor: @@species is not a constructor"));
            return nullptr;
        }
        return speciesVal;
    }

    // Public helper: TypedArraySpeciesCreate(exemplar, length) per ECMA-262
    // 22.2.4.7 — returns a TsTypedArray of the right kind (via the species
    // constructor or default), or nullptr if a TypeError was thrown.
    extern "C" void* ts_typed_array_species_alloc(void* receiver, int64_t length) {
        TsTypedArray* ta = (TsTypedArray*)receiver;
        if (!ta) return nullptr;
        void* defaultCtor = default_ta_ctor_for(ta);
        TsValue* ctorVal = species_constructor((void*)ta, defaultCtor);
        if (!ctorVal) return nullptr;  // TypeError already thrown
        if (ctorVal != (TsValue*)defaultCtor) {
            // Custom species — `new ctor(length)`
            TsValue* lenArg = ts_value_make_int(length);
            TsValue* result = ts_new_from_constructor_1(ctorVal, lenArg);
            if (result) {
                void* resRaw = ts_value_get_object(result);
                if (resRaw) {
                    uint32_t m16 = *(uint32_t*)((char*)resRaw + 16);
                    if (m16 == TsTypedArray::MAGIC) return resRaw;
                }
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "TypedArraySpeciesCreate: result is not a TypedArray"));
                return nullptr;
            }
        }
        // Default path
        return TsTypedArray::Create((size_t)length, ta->GetElementSize(),
                                    ta->IsClamped(), ta->GetType());
    }

    // Helper: given a TsArray result from map/filter on a TypedArray receiver,
    // allocate a TypedArray (via SpeciesConstructor) and copy the elements in.
    // Per ECMA-262 22.2.4.7 TypedArraySpeciesCreate.
    static void* rematerialize_ta_from_array(TsTypedArray* receiver, TsArray* resultArr) {
        if (!receiver) return (void*)resultArr;
        size_t n = resultArr ? (size_t)resultArr->Length() : 0;

        // Try species constructor; fall back to direct TsTypedArray::Create
        // on null or wrong shape.
        void* defaultCtor = default_ta_ctor_for(receiver);
        TsValue* ctorVal = species_constructor((void*)receiver, defaultCtor);
        if (ctorVal && ctorVal != (TsValue*)defaultCtor) {
            // Allocate via `new ctor(n)`
            TsValue* lenArg = ts_value_make_int((int64_t)n);
            TsValue* result = ts_new_from_constructor_1(ctorVal, lenArg);
            if (result) {
                void* resRaw = ts_value_get_object(result);
                if (resRaw) {
                    uint32_t m16 = *(uint32_t*)((char*)resRaw + 16);
                    if (m16 == TsTypedArray::MAGIC) {
                        TsTypedArray* ta = (TsTypedArray*)resRaw;
                        size_t copyN = std::min(n, ta->GetLength());
                        for (size_t i = 0; i < copyN; i++) {
                            ta->Set(i, resultArr->GetElementDouble(i));
                        }
                        return (void*)ta;
                    }
                }
                // Result not a TypedArray — per spec ValidateTypedArray throws.
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "TypedArraySpeciesCreate: result is not a TypedArray"));
                return nullptr;
            }
        }

        // Default path (no custom species, or species was the default ctor):
        // allocate same kind directly.
        if (!resultArr) return (void*)TsTypedArray::Create(0,
            receiver->GetElementSize(), receiver->IsClamped(), receiver->GetType());
        TsTypedArray* ta = TsTypedArray::Create(n,
            receiver->GetElementSize(), receiver->IsClamped(), receiver->GetType());
        for (size_t i = 0; i < n; i++) {
            ta->Set(i, resultArr->GetElementDouble(i));
        }
        return (void*)ta;
    }

    // ECMA-262 ArraySpeciesCreate (23.1.3.3.x): map/filter/slice/splice/concat
    // build their result via O.constructor[Symbol.species] (fallback Array).
    // `resultArr` is the TsArray the existing fast path already computed. If the
    // receiver's species is the default Array constructor (the common case —
    // normal arrays have no own/inherited @@species, so species_constructor
    // returns the default) we return it UNCHANGED (fast path, zero behavior
    // change → no lodash/node regression). For a custom species we Construct it
    // with `len` and CreateDataProperty the elements onto the result. Returns a
    // raw object pointer (callers re-box), or nullptr if species_constructor
    // threw a TypeError. Safe to call from the hot typed path: a plain TsArray
    // with no own "constructor" property fast-rejects below before any property
    // lookup, so normal `number[].map(...)` pays only one null check.
    extern "C" void* ts_array_species_rematerialize(void* receiver, void* resultRaw) {
        TsArray* resultArr = (TsArray*)resultRaw;
        if (!receiver || !resultArr) return resultRaw;
        // Fast reject: a plain TsArray whose only "constructor" is the inherited
        // Array (no own property) uses the default species → skip the protocol.
        {
            uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)receiver);
            void* raw = nanbox_is_ptr(nb) ? nanbox_to_ptr(nb) : receiver;
            if (raw && *(uint32_t*)raw == 0x41525259) {        // TsArray::MAGIC
                TsArray* a = (TsArray*)raw;
                if (!a->properties) return resultRaw;          // no own props
                TsValue ck; ck.type = ValueType::STRING_PTR;
                ck.ptr_val = TsString::GetInterned("constructor");
                if (a->properties->Get(ck).type == ValueType::UNDEFINED)
                    return resultRaw;                          // default species
            }
        }
        void* defaultCtor = ts_get_global_Array();
        if (!defaultCtor) return resultRaw;
        TsValue* ctorVal = species_constructor(receiver, defaultCtor);
        if (!ctorVal) return nullptr;                          // TypeError thrown
        if (ctorVal == (TsValue*)defaultCtor) return resultRaw;  // FAST PATH

        int64_t n = resultArr->Length();
        TsValue* A = ts_new_from_constructor_1(ctorVal, ts_value_make_int(n));
        if (!A) return resultRaw;                              // degrade gracefully
        for (int64_t i = 0; i < n; i++) {
            TsValue* el = (TsValue*)resultArr->GetElementBoxed((size_t)i);
            ts_object_set_property((void*)A, (void*)ts_value_make_int(i), (void*)el);
        }
        void* raw = ts_value_get_object(A);
        return raw ? raw : resultRaw;
    }

    void* ts_array_map(void* arr, void* callback, void* thisArg) {
        if (TsTypedArray* ta = try_as_typed_array(arr)) {
            TsValue* argvBuf[2] = { (TsValue*)callback, (TsValue*)thisArg };
            TsValue* res = ts_array_map_native(arr, 2, argvBuf);
            void* raw = res ? ts_value_get_object(res) : nullptr;
            return rematerialize_ta_from_array(ta, (TsArray*)raw);
        }
        if (!array_require_callable(callback, "map")) return nullptr;
        void* result = ((TsArray*)arr)->Map(callback, thisArg);
        return ts_array_species_rematerialize(arr, result);  // ECMA-262 ArraySpeciesCreate
    }

    void* ts_array_filter(void* arr, void* callback, void* thisArg) {
        if (TsTypedArray* ta = try_as_typed_array(arr)) {
            TsValue* argvBuf[2] = { (TsValue*)callback, (TsValue*)thisArg };
            TsValue* res = ts_array_filter_native(arr, 2, argvBuf);
            void* raw = res ? ts_value_get_object(res) : nullptr;
            return rematerialize_ta_from_array(ta, (TsArray*)raw);
        }
        // Inverted dispatch (V8 `Cast<FastJSArray> otherwise <generic>`): the C++
        // native fast path is spec-correct for a PACKED array (no holes → no
        // inherited-index fill). A HOLEY array may have an inherited index
        // (Array.prototype[k]) supplying a held value per spec [[Get]], which the
        // native skips — so delegate those to the self-hosted spec impl. Hot dense
        // arrays stay on the C++ loop; only holey arrays pay the slow path.
        if (g_selfhosted_filter && g_array_proto_has_indexed &&
            arr && *(uint32_t*)arr == TsArray::MAGIC && ((TsArray*)arr)->HasHoles()) {
            // SH(receiver, callbackfn, thisArg) — receiver is an explicit arg, not `this`.
            extern TsValue* ts_call_with_this_3(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3);
            TsValue* res = ts_call_with_this_3(ts_value_make_object(g_selfhosted_filter), ts_value_make_undefined(),
                                               ts_value_make_object(arr), (TsValue*)callback, (TsValue*)thisArg);
            return res ? ts_value_get_object(res) : nullptr;
        }
        if (!array_require_callable(callback, "filter")) return nullptr;
        void* result = ((TsArray*)arr)->Filter(callback, thisArg);
        return ts_array_species_rematerialize(arr, result);  // ECMA-262 ArraySpeciesCreate
    }

    // IsCallable check shared across array methods (forEach/map/filter/...).
    // Per ECMA-262 each of these methods expects a callable as the first arg
    // and must throw TypeError otherwise. The C-level entry points are
    // reached directly from compiler-emitted IR (bypassing the *_native
    // wrappers), so the check must live here too.
    static bool array_require_callable(void* callback, const char* name) {
        if (ts_is_callable(callback)) {  // canonical IsCallable (TsObject.cpp)
            return true;
        }
        char msg[160];
        snprintf(msg, sizeof(msg),
            "Array.prototype.%s callback is not a function", name);
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
        return false;  // unreachable
    }

    // Shared spec preamble for reduce/reduceRight:
    // - callback must be callable (else TypeError)
    // - if no initial value and array is empty, TypeError
    // Returns true if preamble passed; caller proceeds. ts_throw uses
    // longjmp so a false return is only reached when no handler is live.
    static bool reduce_spec_preamble(TsArray* a, void* callback, void* initialValue,
                                     const char* name) {
        if (!array_require_callable(callback, name)) return false;
        if (!initialValue && a->Length() == 0) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Reduce of empty array with no initial value"));
            return false;  // unreachable
        }
        return true;
    }

    extern TsValue* ts_array_reduce_native(void* ctx, int argc, TsValue** argv);
    extern TsValue* ts_array_reduceRight_native(void* ctx, int argc, TsValue** argv);

    void* ts_array_reduce(void* arr, void* callback, void* initialValue) {
        if (try_as_typed_array(arr)) {
            int argc = initialValue ? 2 : 1;
            TsValue* argvBuf[2] = { (TsValue*)callback, (TsValue*)initialValue };
            return (void*)ts_array_reduce_native(arr, argc, argvBuf);
        }
        TsArray* a = (TsArray*)arr;
        if (!reduce_spec_preamble(a, callback, initialValue, "reduce")) return nullptr;
        return a->Reduce(callback, initialValue);
    }

    void* ts_array_reduceRight(void* arr, void* callback, void* initialValue) {
        if (try_as_typed_array(arr)) {
            int argc = initialValue ? 2 : 1;
            TsValue* argvBuf[2] = { (TsValue*)callback, (TsValue*)initialValue };
            return (void*)ts_array_reduceRight_native(arr, argc, argvBuf);
        }
        TsArray* a = (TsArray*)arr;
        if (!reduce_spec_preamble(a, callback, initialValue, "reduceRight")) return nullptr;
        return a->ReduceRight(callback, initialValue);
    }

    extern TsValue* ts_array_some_native(void* ctx, int argc, TsValue** argv);
    extern TsValue* ts_array_every_native(void* ctx, int argc, TsValue** argv);
    extern TsValue* ts_array_find_native(void* ctx, int argc, TsValue** argv);
    extern TsValue* ts_array_findIndex_native(void* ctx, int argc, TsValue** argv);
    extern TsValue* ts_array_findLast_native(void* ctx, int argc, TsValue** argv);
    extern TsValue* ts_array_findLastIndex_native(void* ctx, int argc, TsValue** argv);

    bool ts_array_some(void* arr, void* callback, void* thisArg) {
        void* rawArr = ts_nanbox_safe_unbox(arr);
        if (!arr_is_tsarray(rawArr)) {
            TsValue* argvBuf[2] = { (TsValue*)callback, (TsValue*)thisArg };
            TsValue* res = ts_array_some_native(arr, 2, argvBuf);
            return res ? ts_value_to_bool(res) : false;
        }
        if (!array_require_callable(callback, "some")) return false;
        return ((TsArray*)rawArr)->Some(callback, thisArg);
    }

    bool ts_array_every(void* arr, void* callback, void* thisArg) {
        void* rawArr = ts_nanbox_safe_unbox(arr);
        if (!arr_is_tsarray(rawArr)) {
            TsValue* argvBuf[2] = { (TsValue*)callback, (TsValue*)thisArg };
            TsValue* res = ts_array_every_native(arr, 2, argvBuf);
            return res ? ts_value_to_bool(res) : true;
        }
        if (!array_require_callable(callback, "every")) return false;
        return ((TsArray*)rawArr)->Every(callback, thisArg);
    }

    TsValue* ts_array_find(void* arr, void* callback, void* thisArg) {
        void* rawArr = ts_nanbox_safe_unbox(arr);
        if (!arr_is_tsarray(rawArr)) {
            TsValue* argvBuf[2] = { (TsValue*)callback, (TsValue*)thisArg };
            return ts_array_find_native(arr, 2, argvBuf);
        }
        if (!array_require_callable(callback, "find")) return ts_value_make_undefined();
        return ((TsArray*)rawArr)->Find(callback, thisArg);
    }

    int64_t ts_array_findIndex(void* arr, void* callback, void* thisArg) {
        void* rawArr = ts_nanbox_safe_unbox(arr);
        if (!arr_is_tsarray(rawArr)) {
            TsValue* argvBuf[2] = { (TsValue*)callback, (TsValue*)thisArg };
            TsValue* res = ts_array_findIndex_native(arr, 2, argvBuf);
            return res ? ts_value_get_int(res) : -1;
        }
        if (!array_require_callable(callback, "findIndex")) return -1;
        return ((TsArray*)rawArr)->FindIndex(callback, thisArg);
    }

    TsValue* ts_array_findLast(void* arr, void* callback, void* thisArg) {
        void* rawArr = ts_nanbox_safe_unbox(arr);
        if (!arr_is_tsarray(rawArr)) {
            TsValue* argvBuf[2] = { (TsValue*)callback, (TsValue*)thisArg };
            return ts_array_findLast_native(arr, 2, argvBuf);
        }
        if (!array_require_callable(callback, "findLast")) return ts_value_make_undefined();
        return ((TsArray*)rawArr)->FindLast(callback, thisArg);
    }

    int64_t ts_array_findLastIndex(void* arr, void* callback, void* thisArg) {
        void* rawArr = ts_nanbox_safe_unbox(arr);
        if (!arr_is_tsarray(rawArr)) {
            TsValue* argvBuf[2] = { (TsValue*)callback, (TsValue*)thisArg };
            TsValue* res = ts_array_findLastIndex_native(arr, 2, argvBuf);
            return res ? ts_value_get_int(res) : -1;
        }
        if (!array_require_callable(callback, "findLastIndex")) return -1;
        return ((TsArray*)rawArr)->FindLastIndex(callback, thisArg);
    }

    static void* ts_array_concat_impl(void* arr, void* other) {
        // Unbox if arr is a TsValue* (boxed array)
        void* rawArr = ts_nanbox_safe_unbox(arr);
        // Guard non-TsArray receivers. The compiler emits ts_array_concat
        // for any `x.concat(y)` call, but if `x` isn't actually an Array
        // (e.g. lodash's `_.concat(...)` where `_` is the lodash function
        // object that has its own `concat` property), we MUST look up
        // `concat` on the receiver and call that, not coerce `x` into
        // array-like form. Otherwise `_.concat([1], [2])` would unwrap
        // `_` as if it were an array and produce nonsense.
        if (rawArr) {
            uintptr_t p = (uintptr_t)rawArr;
            uint32_t magic = 0;
            if (p > 0x1000 && p < 0x0000800000000000ULL) {
                magic = *(uint32_t*)rawArr;
            }
            if (magic != TsArray::MAGIC) {
                extern TsValue* ts_object_get_property(void* obj, const char* key);
                TsValue* concatProp = ts_object_get_property(rawArr, "concat");
                if (concatProp && !ts_value_is_undefined(concatProp)) {
                    extern TsValue* ts_call_with_this_1(TsValue* fn, TsValue* thisArg, TsValue* arg1);
                    TsValue* res = ts_call_with_this_1(concatProp, (TsValue*)arr, (TsValue*)other);
                    return res ? (void*)ts_value_get_object(res) : nullptr;
                }
                // No `concat` property — fall back to array-like materialization
                // (matches existing behavior for array-likes).
                TsValue* argvBuf[1] = { (TsValue*)other };
                TsValue* res = ts_array_concat_native(arr, 1, argvBuf);
                return res ? (void*)ts_value_get_object(res) : (void*)ts_array_create();
            }
        }
        TsArray* first = (TsArray*)rawArr;

        // Per spec: if `other` is not an Array (or spreadable), it's appended
        // as a SINGLE element (not spread). Detect NaN-boxed primitives first
        // — they can't be unboxed to a dereferenceable pointer.
        {
            uint64_t oNb = (uint64_t)(uintptr_t)other;
            if (nanbox_is_undefined(oNb) || nanbox_is_null(oNb) ||
                nanbox_is_int32(oNb) || nanbox_is_double(oNb) ||
                nanbox_is_bool(oNb)) {
                // Append as single element.
                TsArray* result = TsArray::Create(first->Length() + 1);
                for (size_t i = 0; i < first->Length(); ++i) {
                    result->Push(first->Get(i));
                }
                result->Push((int64_t)other);
                return result;
            }
        }

        void* rawOther = ts_nanbox_safe_unbox(other);

        // Check if 'other' is a TsArray (magic 0x41525259 at offset 0)
        bool otherIsArray = rawOther && *(uint32_t*)rawOther == TsArray::MAGIC;

        if (otherIsArray) {
            // Fast path: array-to-array concat
            TsArray* second = (TsArray*)rawOther;
            size_t totalLen = first->Length() + second->Length();
            TsArray* result = TsArray::Create(totalLen);
            for (size_t i = 0; i < first->Length(); ++i) {
                result->Push(first->Get(i));
            }
            for (size_t i = 0; i < second->Length(); ++i) {
                result->Push(second->Get(i));
            }
            return result;
        }

        // Per ES spec IsConcatSpreadable(O):
        //   1. If O has Symbol.isConcatSpreadable, use ToBoolean of that value.
        //   2. Else if Array.isArray(O), spread.
        //   3. Otherwise, NOT spreadable — append as single element.
        // Well-known symbols are registered as canonical string keys like
        // "[Symbol.isConcatSpreadable]" (see ts_get_global_Symbol).
        bool isSpreadable = false;
        bool spreadableExplicit = false;
        if (rawOther) {
            TsValue* sval = ts_object_get_property(rawOther, "[Symbol.isConcatSpreadable]");
            if (sval) {
                uint64_t snb = nanbox_from_tsvalue_ptr(sval);
                if (!nanbox_is_undefined(snb)) {
                    spreadableExplicit = true;
                    isSpreadable = ts_value_to_bool(sval);
                }
            }
        }

        if (spreadableExplicit && isSpreadable) {
            // Spread via length + indexed reads.
            TsValue* lenVal = ts_object_get_property(rawOther, "length");
            double lenD = lenVal ? ts_to_number(lenVal) : 0;
            int64_t len = 0;
            if (lenD == lenD && lenD > 0) {
                if (lenD > (double)(1LL << 20)) lenD = (double)(1LL << 20);
                len = (int64_t)lenD;
            }
            TsArray* result = TsArray::Create(first->Length() + len);
            for (size_t i = 0; i < first->Length(); ++i) {
                result->Push(first->Get(i));
            }
            for (int64_t i = 0; i < len; i++) {
                char key[32];
                snprintf(key, sizeof(key), "%lld", (long long)i);
                TsValue* elem = ts_object_get_property(rawOther, key);
                if (!elem) elem = ts_value_make_undefined();
                result->Push((int64_t)(uintptr_t)elem);
            }
            return result;
        }

        // Default: not spreadable — append as single element.
        TsArray* result = TsArray::Create(first->Length() + 1);
        for (size_t i = 0; i < first->Length(); ++i) {
            result->Push(first->Get(i));
        }
        result->Push((int64_t)(uintptr_t)other);
        return result;
    }

    void* ts_array_concat(void* arr, void* other) {
        void* result = ts_array_concat_impl(arr, other);
        // ECMA-262 23.1.3.2: the concat result is built via ArraySpeciesCreate(O,0).
        // Apply species ONLY for a real Array receiver — the impl's non-array
        // paths delegate to the receiver's own concat and must not be re-specied.
        void* raw = ts_nanbox_safe_unbox(arr); if (!raw) raw = arr;
        uintptr_t p = (uintptr_t)raw;
        if (raw && p > 0x1000 && p < 0x0000800000000000ULL &&
            *(uint32_t*)raw == TsArray::MAGIC) {
            return ts_array_species_rematerialize(arr, result);
        }
        return result;
    }

    // Spread an iterable into an array (used for array-literal `[...iterable]`).
    // Unlike ts_array_concat (which uses IsConcatSpreadable per ECMA-262
    // 23.1.3.2 Array.prototype.concat), array-literal spread uses the
    // iterator protocol (ECMA-262 13.2.4.1, SpreadElement evaluation):
    // call @@iterator and loop next() until done.
    //
    // For TsArray inputs we still take the fast path; for generators and
    // other iterables we iterate. Mutates `arr` in place and returns it.
    void* ts_array_spread_into(void* arr, void* iterable) {
        void* rawArr = ts_nanbox_safe_unbox(arr);
        if (!rawArr) return arr;
        if (*(uint32_t*)rawArr != TsArray::MAGIC) return arr;
        TsArray* dst = (TsArray*)rawArr;

        // Fast path: TsArray source.
        void* rawIter = ts_nanbox_safe_unbox(iterable);
        if (rawIter) {
            uintptr_t p = (uintptr_t)rawIter;
            if (p > 0x1000 && p < 0x0000800000000000ULL) {
                uint32_t mg = *(uint32_t*)rawIter;
                // Fast path only when Array.prototype[@@iterator] is pristine.
                // After a user override/delete (version != 0), fall through to
                // the iterator protocol so the mutation is honored / throws.
                if (mg == TsArray::MAGIC && g_array_prototype_version == 0) {
                    TsArray* src = (TsArray*)rawIter;
                    for (size_t i = 0; i < src->Length(); ++i) {
                        dst->Push(src->Get(i));
                    }
                    return dst;
                }
            }
        }

        // Iterator protocol fallback.
        TsValue* iterator = ts::ts_iterator_get((TsValue*)iterable);
        if (!iterator) return dst;
        for (;;) {
            TsValue* result = ts::ts_iterator_next(iterator, nullptr);
            if (!result || ts::ts_iterator_result_done(result)) break;
            TsValue* val = ts::ts_iterator_result_value(result);
            dst->Push((int64_t)(uintptr_t)val);
        }
        return dst;
    }

    // ECMA-262 ArrayBindingPattern / ArrayAssignmentPattern use the ITERATOR
    // protocol, not index access. Materialize the first `maxCount` iterator
    // values (or ALL values when `hasRest`) into a fresh real TsArray, which the
    // destructuring lowering then reads by index. This makes `[a,b]=customIter`
    // work (previously yielded undefined because the source was indexed as an
    // array) and propagates a throw from the iterator's next(). Real arrays go
    // through their array iterator here too, so behavior is unchanged for them.
    void* ts_destructure_iterate(void* source, int64_t maxCountArg, int64_t hasRestArg) {
        // The HIR lowering passes maxCount/hasRest as NaN-boxed constants (the
        // generic runtime-call ABI boxes Int operands to `ptr`), so the raw
        // int64 we receive here is a NaN-boxed bit pattern, not the literal
        // count. Decode it. (Previously masked: ts_iterator_result_done used to
        // mis-read user-iterator results as done after 1 step, accidentally
        // capping consumption at 1 regardless of maxCount; with that fixed an
        // un-decoded giant maxCount made `[a,b]=iter` consume to exhaustion ->
        // infinite loop on a never-`done` iterator.)
        auto decodeCount = [](int64_t v) -> int64_t {
            uint64_t nb = (uint64_t)v;
            if (nanbox_is_int32(nb)) return (int64_t)nanbox_to_int32(nb);
            if (nanbox_is_double(nb)) return (int64_t)nanbox_to_double(nb);
            return v;  // already a raw count (defensive)
        };
        int64_t maxCount = decodeCount(maxCountArg);
        int64_t hasRest = decodeCount(hasRestArg);

        TsArray* dst = TsArray::Create(0);
        TsValue* iter = ts::ts_iterator_get((TsValue*)source);
        // ECMA-262 GetIterator: a source with no (or non-callable) @@iterator is
        // not iterable -> throw TypeError. ts_iterator_get falls back to
        // returning the SOURCE itself for a non-iterable, so a null iter OR an
        // iter that isn't a heap object with a callable `next` means "not
        // iterable" (was a silent best-effort empty -> mis-bound `var [a]=5`).
        // Only reached for non-empty patterns (empty binding `[]` returns before
        // this call; null/undefined is caught by ts_destructure_require_object).
        {
            bool isIterable = false;
            void* rawIter = iter ? ts_value_get_object(iter) : nullptr;
            if (rawIter && (uintptr_t)rawIter >= 0x1000 &&
                (uintptr_t)rawIter < 0x0000800000000000ULL) {
                extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
                TsValue* nx = ts_object_get_property(rawIter, "next");
                if (nx) {
                    TsValue nv = nanbox_to_tagged(nx);
                    isIterable = (nv.type == ValueType::OBJECT_PTR ||
                                  nv.type == ValueType::FUNCTION_PTR) && nv.ptr_val;
                }
            }
            if (!isIterable) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "source is not iterable"));
                return dst;  // unreachable (ts_throw longjmps)
            }
        }
        int64_t i = 0;
        bool iteratorDone = false;
        while (hasRest || i < maxCount) {
            TsValue* result = ts::ts_iterator_next(iter, nullptr);
            if (!result || ts::ts_iterator_result_done(result)) { iteratorDone = true; break; }
            dst->Push((int64_t)(uintptr_t)ts::ts_iterator_result_value(result));
            i++;
        }
        // ECMA-262 13.3.3.6: when an ArrayBindingPattern stops before the iterator
        // is exhausted (iteratorRecord.[[done]] is still false), perform
        // IteratorClose -> call the iterator's return(). A rest element consumes to
        // exhaustion (iteratorDone), so it never closes; a fixed-length pattern that
        // stopped at maxCount without seeing done must close.
        if (!iteratorDone && !hasRest && iter) {
            extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
            extern TsValue* ts_call_with_this_0(TsValue* boxedFunc, TsValue* thisArg);
            void* rawIter = ts_value_get_object(iter);
            if (!rawIter) rawIter = iter;
            if ((uintptr_t)rawIter >= 0x1000) {
                TsValue* retFn = ts_object_get_property(rawIter, "return");
                if (retFn) {
                    TsValue rf = nanbox_to_tagged(retFn);
                    if ((rf.type == ValueType::OBJECT_PTR || rf.type == ValueType::FUNCTION_PTR)
                        && rf.ptr_val) {
                        TsValue* res = ts_call_with_this_0(retFn, iter);
                        // ECMA-262 7.4.11 IteratorClose (normal completion): if
                        // the iterator's return() yields a non-Object value,
                        // throw a TypeError. (A return() that itself throws
                        // propagates normally through ts_call_with_this_0.)
                        TsValue rv = res ? nanbox_to_tagged(res) : TsValue();
                        // Must be an Object. `null` is carried as OBJECT_PTR with
                        // a null ptr_val, so the bare type check let it through —
                        // require a non-null pointer (Type(value) is Object).
                        bool isObject = (rv.type == ValueType::OBJECT_PTR ||
                                         rv.type == ValueType::ARRAY_PTR ||
                                         rv.type == ValueType::FUNCTION_PTR) &&
                                        rv.ptr_val != nullptr;
                        if (!isObject) {
                            ts_throw((TsValue*)ts_error_create_typed(
                                "TypeError",
                                "iterator result return() is not an object"));
                        }
                    }
                }
            }
        }
        return dst;
    }

    bool ts_array_is_array(void* value) {
        if (!value) return false;

        // Decode NaN-boxed value. Mirror ts_array_isArray: a RegExp match array
        // (RMAT) is an Array exotic object, and arrays can arrive boxed as
        // OBJECT_PTR. Without this, lodash's `Array.isArray(/x/.exec(...))`
        // returned false -> baseClone skipped initCloneArray -> match-array
        // clone lost .index/.input/elements.
        // An `arguments` object is a branded TsArray that is array-LIKE but NOT
        // an Array — Array.isArray(arguments) must be false (ECMA-262 / lodash
        // _.isArguments vs _.isArray).
        auto isArgs = [](void* p) -> bool {
            return p && *(uint32_t*)p == TsArray::MAGIC && ((TsArray*)p)->isArguments;
        };
        TsValue decoded = nanbox_to_tagged((TsValue*)value);
        if (decoded.type == ValueType::ARRAY_PTR) return !isArgs(decoded.ptr_val);
        if (decoded.type == ValueType::OBJECT_PTR && decoded.ptr_val) {
            uint32_t magic = *(uint32_t*)decoded.ptr_val;
            if (magic == TsArray::MAGIC) return !isArgs(decoded.ptr_val);
            return magic == 0x524D4154; // TsRegExpMatchArray
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
        // Spec: Array.from(null) or Array.from(undefined) must throw TypeError
        // (can't create iterator from nullish). Do this check first before
        // any other handling.
        uint64_t preCheckNB = (uint64_t)(uintptr_t)arrayLike;
        if (!arrayLike || nanbox_is_null(preCheckNB) || nanbox_is_undefined(preCheckNB)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Array.from requires an iterable or array-like object"));
            return ts_array_create();  // unreachable
        }

        // Spec (ECMA-262 22.1.2.1): If mapfn is undefined, let mapping be false.
        // Otherwise IsCallable(mapfn) is checked; if false, throw TypeError.
        // Null is NOT treated as undefined here — it must throw.
        if (mapFn) {
            uint64_t mfNB = (uint64_t)(uintptr_t)mapFn;
            if (nanbox_is_undefined(mfNB)) {
                mapFn = nullptr;
            } else {
                bool isCallable = ts_is_callable(mapFn);  // canonical IsCallable
                if (!isCallable) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "Array.from: when provided, mapFn must be callable"));
                    return ts_array_create();  // unreachable
                }
            }
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
                    // Call the map function: mapFn(elem, index). NOTE: box via
                    // nanbox_from_tagged(elem) -- ts_value_box_any(&elem) was wrong
                    // (it takes a value/object pointer, but &elem is the stack
                    // address of the TsValue struct, so it read garbage and the
                    // element arrived as {}/0, e.g. Array.from([1,2,3],x=>x*10)=0,0,0).
                    TsValue* elemBoxed = nanbox_from_tagged(elem);
                    TsValue* indexVal = ts_value_make_int(i);
                    TsValue* mapped = tsCall(mapFnVal, elemBoxed, indexVal);
                    // `result` was pre-sized to length N (Create(srcArr->Length()))
                    // for the set_v(i) path below; Push() appended PAST that, so the
                    // mapped values landed at indices N..2N-1 and Array.from(arr,fn)
                    // returned the N leading holes ([0,0,0]). Store at index i.
                    if (mapped) {
                        ts_array_set_v(result, i, nanbox_to_tagged(mapped));
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

            // ECMA-262 22.1.2.1: Array.from(string) iterates via the String
            // iterator, which yields CODE POINTS (not UTF-16 code units), so an
            // astral character (surrogate pair) is ONE element. UTF-8 already
            // encodes a full code point per multibyte sequence, so walk it
            // sequence-by-sequence and stop at end-of-string. (The old loop ran
            // str->Length() — the UTF-16 unit count — while the inner walk
            // counted code points, so an astral char emitted a trailing ""
            // element: Array.from("\u{1F600}") wrongly gave ["..", ""] len 2.
            // Now matches for-of / spread, which were already correct.)
            TsArray* result = TsArray::Create(0);
            const char* p = utf8;
            int64_t i = 0;
            while (*p) {
                int clen;
                if ((*p & 0x80) == 0) clen = 1;
                else if ((*p & 0xE0) == 0xC0) clen = 2;
                else if ((*p & 0xF0) == 0xE0) clen = 3;
                else if ((*p & 0xF8) == 0xF0) clen = 4;
                else clen = 1;

                char charBuf[5] = {0}; // UTF-8 code point is up to 4 bytes
                memcpy(charBuf, p, clen);
                p += clen;

                TsString* charStr = TsString::Create(charBuf);
                TsValue* charVal = ts_value_make_string(charStr);

                if (hasMapFn) {
                    TsValue* indexVal = ts_value_make_int(i);
                    TsValue* mapped = tsCall(mapFnVal, charVal, indexVal);
                    if (mapped) {
                        result->Push((int64_t)mapped);
                    } else {
                        result->Push((int64_t)charVal);
                    }
                } else {
                    result->Push((int64_t)charVal);
                }
                i++;
            }
            return result;
        }

        // Set/Map collections: drive the iterator protocol via the shared
        // ts_iterator_get/ts_iterator_next (which now yields Set values / Map
        // entries). The bespoke @@iterator+length handling below couldn't reach
        // their prototype-exposed [Symbol.iterator], so Array.from(set)/
        // Array.from(map) came out empty.
        {
            void* rawCheck = ts_nanbox_safe_unbox(arrayLike);
            if (!rawCheck) rawCheck = rawPtr;
            if (rawCheck && (uintptr_t)rawCheck >= 0x1000) {
                uint32_t m16 = *(uint32_t*)((char*)rawCheck + 16);
                bool isIterableColl = (m16 == 0x53455453) ||  // TsSet "SETS"
                                      (m16 == 0x47454E52) ||  // TsGenerator "GENR"
                                      (m16 == 0x4D415053 && ((TsMap*)rawCheck)->IsExplicitMap());  // explicit Map
                if (isIterableColl) {
                    TsValue* iterator = ts::ts_iterator_get((TsValue*)arrayLike);
                    TsArray* result = TsArray::Create(0);
                    int64_t i = 0;
                    if (iterator) {
                        for (;;) {
                            TsValue* res = ts::ts_iterator_next(iterator, nullptr);
                            if (!res || ts::ts_iterator_result_done(res)) break;
                            TsValue* val = ts::ts_iterator_result_value(res);
                            if (hasMapFn) {
                                TsValue* indexVal = ts_value_make_int(i);
                                TsValue* mapped = tsCall(mapFnVal, val, indexVal);
                                result->Push((int64_t)(uintptr_t)(mapped ? mapped : val));
                            } else {
                                result->Push((int64_t)(uintptr_t)val);
                            }
                            i++;
                        }
                    }
                    return result;
                }
            }
        }

        // Check if it's an object with a 'length' property (array-like)
        if (is_flat_object(rawPtr)) {
            rawPtr = ts_flat_object_to_map(rawPtr);
        }
        TsMap* map = dynamic_cast<TsMap*>((TsObject*)rawPtr);
        // ECMA-262 22.1.2.1 Array.from step 1-2: GetMethod(items, @@iterator).
        // If present (and not undefined), call it and validate the result is
        // an Object. The IsHTMLDDA-emulates-undefined cluster relies on
        // this Type(iterator) check throwing TypeError when the @@iterator
        // method returns undefined (e.g., $262.IsHTMLDDA).
        if (map) {
            TsValue iterKey;
            iterKey.type = ValueType::STRING_PTR;
            iterKey.ptr_val = TsString::Create("[Symbol.iterator]");
            TsValue iterMethod = map->Get(iterKey);
            // GetMethod step 2: if undefined or null, treat as no iterator.
            // Note: an [[IsHTMLDDA]] value in this slot is handled here per
            // GetMethod's strict undefined check (it's neither === undefined
            // nor === null), so we proceed to call it.
            if (iterMethod.type == ValueType::FUNCTION_PTR ||
                iterMethod.type == ValueType::OBJECT_PTR) {
                TsValue* methodVal = nanbox_from_tagged(iterMethod);
                // Verify it's actually callable (function/closure/proxy).
                bool isCallable = ts_is_callable(methodVal);  // canonical IsCallable
                if (isCallable) {
                    // Invoke the @@iterator method with `this` = items.
                    TsValue* itemsBoxed = ts_value_make_object(rawPtr);
                    TsValue* iterator = ts_call_with_this_0(methodVal, itemsBoxed);
                    // ECMA-262 7.4.2 GetIterator: Type(iterator) must be Object.
                    bool iterIsObject = false;
                    if (iterator) {
                        uint64_t inb = (uint64_t)(uintptr_t)iterator;
                        if (nanbox_is_ptr(inb) && inb > NANBOX_UNDEFINED) {
                            void* iterRaw = nanbox_to_ptr(inb);
                            if (iterRaw) {
                                // Strings are technically Object via boxing,
                                // but here the iterator-protocol expects a
                                // real iterator object — strings as
                                // iterators isn't the test262 case.
                                iterIsObject = true;
                            }
                        }
                    }
                    if (!iterIsObject) {
                        ts_throw((TsValue*)ts_error_create_typed("TypeError",
                            "Result of @@iterator method is not an object"));
                        return ts_array_create();  // unreachable
                    }
                    // Iterator is valid — fall through to existing iterator
                    // machinery (we don't fully implement protocol-driven
                    // iteration here yet; the type-check is the main spec
                    // gate the cluster needs).
                }
            }
        }
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
                            TsValue* mapped = tsCall(mapFnVal, elemBoxed, indexVal);
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

        // Capacity-safe read: for a sparse array an index in [capacity, length)
        // is a hole / sparse-store entry, not an in-bounds dense slot.
        int64_t raw_val = array->readSlot(index);
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

        // A hole (sparse region or unwritten slot) reads as undefined.
        if (nb == NANBOX_HOLE) {
            *out_type = (uint8_t)ValueType::UNDEFINED;
            *out_value = 0;
            return;
        }
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
            if (magic0 == 0x53545247 || magic0 == TsConsString::MAGIC) { // TsString/TsConsString
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

        if (TsTypedArray* ta = try_as_typed_array(arr)) {
            int64_t len = (int64_t)ta->GetLength();
            // INT_MIN sentinel = "argument not provided"
            if (start == INT64_MIN) start = 0;
            if (end == INT64_MIN) end = len;
            if (start < 0) start = std::max((int64_t)0, len + start);
            if (end < 0) end = std::max((int64_t)0, len + end);
            if (start >= len) return arr;
            if (end > len) end = len;
            if (start >= end) return arr;
            // Spec: ToNumber(value) first (throws TypeError on Symbol).
            double v = 0;
            if (value) {
                uint64_t nb = (uint64_t)value;
                if (nanbox_is_number(nb)) v = nanbox_to_number(nb);
                else v = ts_to_number((TsValue*)value);
            }
            for (int64_t i = start; i < end; i++) ta->Set((size_t)i, v);
            return arr;
        }

        TsArray* array = (TsArray*)arr;
        int64_t len = (int64_t)array->Length();

        // INT64_MIN sentinel = "argument not provided" (the compiler fast-path
        // passes it for an omitted start/end). The typed-array path above
        // decodes it; the regular-array path did NOT, so `[1,2,3].fill(0)` got
        // end = max(0, len + INT64_MIN) = 0 -> start>=end -> no fill at all.
        if (start == INT64_MIN) start = 0;
        if (end == INT64_MIN) end = len;

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

        if (TsTypedArray* ta = try_as_typed_array(arr)) {
            int64_t len = (int64_t)ta->GetLength();
            if (target == INT64_MIN) target = 0;
            if (start == INT64_MIN) start = 0;
            if (end == INT64_MIN) end = len;
            if (target < 0) target = std::max((int64_t)0, len + target);
            if (start < 0) start = std::max((int64_t)0, len + start);
            if (end < 0) end = std::max((int64_t)0, len + end);
            if (target > len) target = len;
            if (start > len) start = len;
            if (end > len) end = len;
            int64_t count = std::min(end - start, len - target);
            if (count <= 0) return arr;
            std::vector<double> tmp((size_t)count);
            for (int64_t i = 0; i < count; i++) tmp[(size_t)i] = ta->Get((size_t)(start + i));
            for (int64_t i = 0; i < count; i++) ta->Set((size_t)(target + i), tmp[(size_t)i]);
            return arr;
        }

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

    extern TsValue* ts_array_entries_native(void* ctx, int argc, TsValue** argv);
    extern TsValue* ts_array_keys_native(void* ctx, int argc, TsValue** argv);
    extern TsValue* ts_array_values_native(void* ctx, int argc, TsValue** argv);

    // Returns an array of [index, value] pairs (iterator-like behavior)
    void* ts_array_entries(void* arr) {
        void* rawArr = ts_nanbox_safe_unbox(arr);
        if (!arr_is_tsarray(rawArr)) {
            TsValue* res = ts_array_entries_native(arr, 0, nullptr);
            return res ? (void*)ts_value_get_object(res) : (void*)ts_array_create();
        }

        TsArray* array = (TsArray*)rawArr;
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
        void* rawArr = ts_nanbox_safe_unbox(arr);
        if (!arr_is_tsarray(rawArr)) {
            TsValue* res = ts_array_keys_native(arr, 0, nullptr);
            return res ? (void*)ts_value_get_object(res) : (void*)ts_array_create();
        }

        TsArray* array = (TsArray*)rawArr;
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
        void* rawArr = ts_nanbox_safe_unbox(arr);
        if (!arr_is_tsarray(rawArr)) {
            TsValue* res = ts_array_values_native(arr, 0, nullptr);
            return res ? (void*)ts_value_get_object(res) : (void*)ts_array_create();
        }

        TsArray* array = (TsArray*)rawArr;
        int64_t len = (int64_t)array->Length();
        TsArray* values = TsArray::Create(len);

        for (int64_t i = 0; i < len; i++) {
            int64_t val = array->Get(i);
            values->Push(val);
        }

        return values;
    }

    // ============================================================
    // Iterator-returning versions (return TsMap-based iterator with .next())
    // ============================================================

    void* ts_array_entries_iter(void* arr) {
        void* items = ts_array_entries(arr);
        if (!items) return ts_create_array_iterator(ts_array_create());
        return ts_create_array_iterator(items);
    }

    void* ts_array_keys_iter(void* arr) {
        void* items = ts_array_keys(arr);
        if (!items) return ts_create_array_iterator(ts_array_create());
        return ts_create_array_iterator(items);
    }

    void* ts_array_values_iter(void* arr) {
        void* items = ts_array_values(arr);
        if (!items) return ts_create_array_iterator(ts_array_create());
        return ts_create_array_iterator(items);
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

    // Array(arg) constructor called as function - handles JS semantics:
    // Array(n) where n is non-negative integer: create array of length n
    // Array(x) where x is not integer: create array with x as single element
    void* ts_array_constructor(void* arg) {
        if (!arg) return TsArray::Create(0);

        uint64_t nb = (uint64_t)(uintptr_t)arg;

        // Check if arg is a number
        if (nanbox_is_int32(nb)) {
            int64_t n = nanbox_to_int64(nb);
            if (n >= 0) return TsArray::CreateSized((size_t)n);
            // Negative: treat as single element
            TsArray* arr = TsArray::Create(1);
            arr->Push((int64_t)arg);
            return arr;
        }
        if (nanbox_is_double(nb)) {
            double d = nanbox_to_double(nb);
            int64_t n = (int64_t)d;
            if (d == (double)n && n >= 0) return TsArray::CreateSized((size_t)n);
            // Non-integer or negative: single element
            TsArray* arr = TsArray::Create(1);
            arr->Push((int64_t)arg);
            return arr;
        }

        // Non-number: create array with arg as single element
        TsArray* arr = TsArray::Create(1);
        arr->Push((int64_t)arg);
        return arr;
    }

    // Push a NaN-boxed value onto an array (for Array(a, b, c) pattern)
    void ts_array_push_any(void* arr, void* value) {
        if (!arr) return;
        TsArray* array = (TsArray*)arr;
        array->Push((int64_t)value);
    }
}
