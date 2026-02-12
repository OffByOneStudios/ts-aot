#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>

// NaN Boxing: JSC-style pointer-biased encoding
//
// All JavaScript values fit in 8 bytes (uint64_t). Pointers are stored raw.
// Doubles are biased by 2^49 to shift them out of pointer space.
// Int32s use a dedicated tag. Special constants (undefined, null, true, false)
// use small values below the minimum valid pointer address.
//
// Encoding:
//   POINTER:    0x0000_XXXXXXXXXXXX  (top 16 bits = 0, lower 48 = heap address)
//   DOUBLE:     raw_IEEE754 + 2^49   (top 16 bits in [0x0002, 0xFFF2])
//   INT32:      0xFFFE_0000XXXXXXXX  (top 16 bits = 0xFFFE, lower 32 = value)
//   UNDEFINED:  0x0000_00000000000A
//   NULL:       0x0000_000000000002
//   TRUE:       0x0000_000000000007
//   FALSE:      0x0000_000000000006

// === Constants ===

static constexpr uint64_t NANBOX_DOUBLE_OFFSET = 0x0002000000000000ULL;  // 2^49
static constexpr uint64_t NANBOX_INT32_TAG     = 0xFFFE000000000000ULL;
static constexpr uint64_t NANBOX_NUMBER_MASK   = 0xFFFE000000000000ULL;  // top 15 bits

// Special constants (in pointer space but below min valid heap pointer)
static constexpr uint64_t NANBOX_UNDEFINED     = 0x000000000000000AULL;
static constexpr uint64_t NANBOX_NULL          = 0x0000000000000002ULL;
static constexpr uint64_t NANBOX_TRUE          = 0x0000000000000007ULL;
static constexpr uint64_t NANBOX_FALSE         = 0x0000000000000006ULL;

// === Encoding functions ===

inline uint64_t nanbox_double(double d) {
    // Canonicalize NaN to avoid collisions in biased encoding.
    // All NaN bit patterns map to the single canonical quiet NaN.
    if (d != d) d = std::numeric_limits<double>::quiet_NaN();
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof(bits));
    return bits + NANBOX_DOUBLE_OFFSET;
}

inline uint64_t nanbox_int32(int32_t i) {
    return NANBOX_INT32_TAG | (uint64_t)(uint32_t)i;
}

inline uint64_t nanbox_ptr(void* p) {
    return (uint64_t)(uintptr_t)p;
}

inline uint64_t nanbox_bool(bool b) {
    return b ? NANBOX_TRUE : NANBOX_FALSE;
}

// === Type checks ===

inline bool nanbox_is_undefined(uint64_t v) { return v == NANBOX_UNDEFINED; }
inline bool nanbox_is_null(uint64_t v)      { return v == NANBOX_NULL; }
inline bool nanbox_is_true(uint64_t v)      { return v == NANBOX_TRUE; }
inline bool nanbox_is_false(uint64_t v)     { return v == NANBOX_FALSE; }
inline bool nanbox_is_bool(uint64_t v)      { return v == NANBOX_TRUE || v == NANBOX_FALSE; }

// Special constants: undefined, null, true, false (all <= NANBOX_UNDEFINED)
inline bool nanbox_is_special(uint64_t v)   { return v <= NANBOX_UNDEFINED; }

// Pointer: top 16 bits = 0, above special constant range
inline bool nanbox_is_ptr(uint64_t v) {
    return (v & 0xFFFF000000000000ULL) == 0 && v > NANBOX_UNDEFINED;
}

// Int32: top 16 bits = 0xFFFE
inline bool nanbox_is_int32(uint64_t v) {
    return (v & 0xFFFF000000000000ULL) == NANBOX_INT32_TAG;
}

// Number (either int32 or double): any of the top 15 bits set
inline bool nanbox_is_number(uint64_t v) {
    return (v & NANBOX_NUMBER_MASK) != 0;
}

// Double: is a number but not int32
inline bool nanbox_is_double(uint64_t v) {
    return nanbox_is_number(v) && !nanbox_is_int32(v);
}

// === Decoding functions ===

inline double nanbox_to_double(uint64_t v) {
    uint64_t bits = v - NANBOX_DOUBLE_OFFSET;
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

inline int32_t nanbox_to_int32(uint64_t v) {
    return (int32_t)(uint32_t)(v & 0xFFFFFFFFULL);
}

inline void* nanbox_to_ptr(uint64_t v) {
    return (void*)(uintptr_t)v;
}

inline bool nanbox_to_bool(uint64_t v) {
    return v == NANBOX_TRUE;
}

// === Convenience: TsValue* ↔ uint64_t ===
// The ABI trick: TsValue* is really a NaN-boxed uint64_t

struct TaggedValue;
using TsValue = TaggedValue;

inline uint64_t nanbox_from_tsvalue_ptr(TsValue* v) {
    return (uint64_t)(uintptr_t)v;
}

inline TsValue* nanbox_to_tsvalue_ptr(uint64_t v) {
    return (TsValue*)(uintptr_t)v;
}

// Extract a numeric value as double (handles both int32 and double encoding)
inline double nanbox_to_number(uint64_t v) {
    if (nanbox_is_int32(v)) return (double)nanbox_to_int32(v);
    if (nanbox_is_double(v)) return nanbox_to_double(v);
    return 0.0;
}

// Extract a numeric value as int64 (handles both int32 and double encoding)
inline int64_t nanbox_to_int64(uint64_t v) {
    if (nanbox_is_int32(v)) return (int64_t)nanbox_to_int32(v);
    if (nanbox_is_double(v)) return (int64_t)nanbox_to_double(v);
    return 0;
}
