#pragma once

#include <cstdint>
#include <cstddef>
#include "TsObject.h"
#include "TsTyped.h"

/// V8-style element kinds for monomorphic array optimizations
/// Must match the ElementKind enum in src/compiler/analysis/Type.h
enum class ElementKind : uint8_t {
    Unknown = 0,       ///< Type not yet determined
    PackedSmi = 1,     ///< Small integers (31-bit signed)
    PackedDouble = 2,  ///< IEEE 754 doubles
    PackedString = 3,  ///< TsString* pointers only
    PackedObject = 4,  ///< Homogeneous object type
    PackedAny = 5,     ///< Mixed types (generic path)
    HoleySmi = 6,      ///< SMI with holes
    HoleyDouble = 7,   ///< Double with holes
    HoleyAny = 8       ///< Mixed with holes
};

class TsArray {
public:
    static constexpr uint32_t MAGIC = 0x41525259; // "ARRY"

    // Grant the type-tag trait (TsTyped.h) access to the private `magic` member
    // so offsetof(TsArray, magic) can be evaluated without exposing the field.
    template <class> friend struct TsTagOf;

    // Ceiling for eager / automatic dense backing growth. A logical `length`
    // may exceed this (sparse array); indices in [capacity, length) are holes
    // unless present in `sparseElements`. SetLength / CreateSized never
    // eager-allocate beyond this, and element writes past it spill to the
    // sparse store rather than growing the dense buffer. Prevents OOM on
    // `a.length = 2.1e9` while keeping normal arrays fully dense. ~67M elems
    // (512MB) — far above any realistic dense array, far below the 2^32 cap.
    static constexpr size_t kMaxDenseElements = (size_t)1 << 26;

    static TsArray* Create(size_t initialCapacity = 4);
    static TsArray* CreateSized(size_t size);
    static TsArray* CreateSpecialized(size_t size, size_t elementSize, bool isDouble = false);

    void Push(int64_t value);
    int64_t Pop();
    void Unshift(int64_t value);
    int64_t Shift();
    int64_t Get(size_t index);
    int64_t GetUnchecked(size_t index) { return ((int64_t*)elements)[index]; }
    double GetElementDouble(size_t index);  // Get element as double (handles both boxed and raw)
    void Set(size_t index, int64_t value);
    void SetUnchecked(size_t index, int64_t value) { ((int64_t*)elements)[index] = value; }
    void PushDouble(double value);  // Push a double value
    int64_t Length();
    // ECMA-262 §10.4.2.4 ArraySetLength. Truncates when newLength < length,
    // pads with NANBOX_HOLE when newLength > length. Returns false if any
    // truncated slot is non-configurable (per spec); current impl returns
    // true unconditionally since all our array slots are spec-default
    // configurable. The runtime caller is responsible for any TypeError
    // dispatch.
    bool SetLength(size_t newLength);
    void Sort();
    int64_t IndexOf(int64_t value, size_t fromIndex = 0);
    int64_t LastIndexOf(int64_t value, int64_t fromIndex = -1); // -1 == iterate from end
    bool Includes(int64_t value, size_t fromIndex = 0);
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

    // True if index >= length OR the slot holds the NANBOX_HOLE sentinel.
    // Used by spec-compliant iteration methods to skip holes.
    bool IsHole(size_t index) const;
    // Write the NANBOX_HOLE sentinel at index (index must be < length).
    void SetHole(size_t index);

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

    /// Get the element kind
    ElementKind GetElementKind() const { return elementKind_; }

    /// Set the element kind (used during array creation/transition)
    void SetElementKind(ElementKind kind) { elementKind_ = kind; }

    /// True once any real hole (within [0,length)) has been introduced. One-way
    /// (never cleared), conservative: never false when holes exist, may stay true
    /// after a hole is overwritten. Used as the V8-`FastJSArray` fast-path
    /// qualifier — a packed (hole-free) array is spec-correct on the C++ native
    /// builtins; a holey one may need prototype-chain [[Get]] (inherited indices).
    bool HasHoles() const { return has_holes_; }
    void MarkHoley() { has_holes_ = true; }

    /// Check if this array can use the fast SMI path
    bool IsSmiArray() const {
        return elementKind_ == ElementKind::PackedSmi ||
               elementKind_ == ElementKind::HoleySmi;
    }

    /// Check if this array uses the double path
    bool IsDoubleArray() const {
        return elementKind_ == ElementKind::PackedDouble ||
               elementKind_ == ElementKind::HoleyDouble ||
               isDouble;  // Legacy compatibility
    }

    /// Transition to a more general element kind
    void TransitionTo(ElementKind newKind);

    // Init-in-place for stack allocation (escape analysis)
    static void InitInPlace(void* mem, size_t initialCapacity);

    // Returns the receiver to pass as the 3rd callback argument.
    // For temp arrays materialized from an array-like receiver, this is
    // the original ToObject'd receiver; for real arrays, it is `this`.
    inline void* CallbackReceiver() {
        return originalReceiver ? originalReceiver : (void*)this;
    }

    // True when the logical length exceeds the physical dense backing, i.e.
    // indices in [capacity, length) are holes unless in `sparseElements`.
    bool isSparse() const { return length > capacity; }
    // Capacity-safe element read: dense slot if index < capacity, else the
    // sparse store, else NANBOX_HOLE. Callers must already know index < length.
    int64_t readSlot(size_t index) const;
    // Capacity-safe element write: dense if index < capacity; grow dense if
    // index < kMaxDenseElements; otherwise spill to the sparse store.
    void writeSlot(size_t index, int64_t value);

private:
    TsArray(size_t initialCapacity, size_t elementSize = 8);

    uint32_t magic = MAGIC;
    void* elements;
    size_t length;
    size_t capacity;
    size_t elementSize;
    bool isSpecialized = false;
    bool isDouble = false;
    ElementKind elementKind_ = ElementKind::PackedAny;  ///< V8-style element kind
    bool has_holes_ = false;  ///< one-way "this array has a real hole" flag (see HasHoles)

public:
    // Set by require_array_or_throw when materializing a temp array from
    // an array-like receiver, so callback methods can pass the original
    // ToObject'd receiver as the 3rd callback argument (per ECMA-262).
    // Placed at end to preserve sizeof/layout of prior private members.
    void* originalReceiver = nullptr;

    // Named (string-keyed) properties side map. Allocated lazily on first
    // string-key write — `arr.foo = bar` stores into properties->Set("foo", bar).
    // Required for spec-compliant arrays since arrays are exotic objects with
    // both indexed elements AND arbitrary string-keyed properties.
    // Write barrier (`ts_gc_write_barrier`) tracks cross-generation pointers
    // through `properties->Set(...)`; no extra barrier needed here.
    TsMap* properties = nullptr;

    // Sparse element overflow store for huge / sparse arrays. Lazily created
    // when an element write lands at an index >= kMaxDenseElements (or in the
    // [capacity, length) hole region of a capped array). Integer-keyed; holds
    // NaN-boxed values. nullptr for the common dense case. GC-scanned via the
    // conservative full old-gen slot walk (no explicit barrier wiring needed).
    TsMap* sparseElements = nullptr;

    // True for the `arguments` object (built by ts_create_arguments_from_params).
    // An arguments object is array-LIKE but NOT an Array: Array.isArray returns
    // false and Object.prototype.toString brands it [object Arguments]. Placed at
    // the end to preserve the offsets codegen hardcodes for the early fields.
    bool isArguments = false;
};

TS_DECLARE_TAG(TsArray);  // magic at offset 0 (POD); friend grants offsetof access

extern "C" {
    void* ts_array_create();
    void* ts_array_create_sized(int64_t size);
    void ts_array_init_inplace(void* mem, int64_t initial_capacity);  // Placement-new at caller-provided memory
    void* ts_array_create_specialized(int64_t size, int64_t elementSize, bool isDouble);
    void* ts_array_get_elements_ptr(void* arr);
    int64_t ts_array_push(void* arr, void* value);  // returns new length
    void* ts_array_pop(void* arr);
    int64_t ts_array_unshift(void* arr, void* value);  // returns new length
    void* ts_array_shift(void* arr);
    
    TsValue* ts_array_get_as_value(void* arr, int64_t index);
    void* ts_array_get_unchecked(void* arr, int64_t index);
    void ts_array_set_unchecked(void* arr, int64_t index, void* value);
    
    // Value-based API variants - avoid heap allocation
    TsValue ts_array_get_v(void* arr, int64_t index);
    void ts_array_set_v(void* arr, int64_t index, TsValue value);
    int64_t ts_array_length(void* arr);
    void* ts_array_sort(void* arr, void* comparator);  // Returns array, handles TsClosure
    void ts_array_sort_with_comparator(void* arr, void* comparator);  // Legacy
    void* ts_array_slice(void* arr, int64_t start, int64_t end);
    void* ts_array_flat(void* arr, int64_t depth);
    void* ts_array_flatMap(void* arr, void* callback, void* thisArg);
    int64_t ts_array_indexOf(void* arr, int64_t value);
    int64_t ts_array_indexOf_from(void* arr, int64_t value, double fromIndex);
    int64_t ts_array_lastIndexOf(void* arr, int64_t value);
    int64_t ts_array_lastIndexOf_from(void* arr, int64_t value, double fromIndex);
    bool ts_array_includes(void* arr, int64_t value);
    bool ts_array_includes_from(void* arr, int64_t value, double fromIndex);
    void* ts_array_at(void* arr, int64_t index);
    void* ts_array_join(void* arr, void* separator);
    void* ts_array_concat(void* arr, void* other);
    void* ts_array_reverse(void* arr);
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
    void* ts_array_of(void* elementsArray);
    void* ts_array_fill(void* arr, void* value, int64_t start, int64_t end);
    void* ts_array_copyWithin(void* arr, int64_t target, int64_t start, int64_t end);
    void* ts_array_entries(void* arr);
    void* ts_array_keys(void* arr);
    void* ts_array_values(void* arr);

    // Iterator-returning versions (return TsMap-based iterator with .next())
    void* ts_array_entries_iter(void* arr);
    void* ts_array_keys_iter(void* arr);
    void* ts_array_values_iter(void* arr);

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

    // Element kind API (V8-style optimization)
    uint8_t ts_array_get_element_kind(void* arr);
    void ts_array_set_element_kind(void* arr, uint8_t kind);
    void ts_array_transition_to(void* arr, uint8_t newKind);
    void* ts_array_create_with_kind(int64_t size, uint8_t kind);

    // Fast path accessors for specific element kinds
    int64_t ts_array_get_smi(void* arr, int64_t index);      // Fast SMI get (no boxing)
    void ts_array_set_smi(void* arr, int64_t index, int64_t value);  // Fast SMI set
    double ts_array_get_double_fast(void* arr, int64_t index);  // Fast double get
    void ts_array_set_double_fast(void* arr, int64_t index, double value);  // Fast double set
    void* ts_array_constructor(void* arg);  // Array(arg) - JS constructor semantics
    void ts_array_push_any(void* arr, void* value);  // Push NaN-boxed value
}
