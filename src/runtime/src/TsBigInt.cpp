#include "TsBigInt.h"
#include "TsRuntime.h"
#include "TsString.h"
#include "TsMap.h"
#include "TsGC.h"
#include "GC.h"
#include <cstring>
#include <string>
#include <cstring>
#include <cmath>

// Cross-TU runtime symbols (declared at file scope per runtime-safety rules;
// block-scope `extern "C"` is illegal here).
extern "C" void* ts_error_create_typed(const char* name, const char* message);
extern "C" void* ts_get_call_this();
extern "C" double ts_to_number(TsValue* v);

static bool tommath_initialized = false;

static void* mp_alloc(size_t size) {
    return ts_alloc(size);
}

static void* mp_realloc(void* ptr, size_t old_size, size_t new_size) {
    return ts_gc_realloc(ptr, old_size, new_size);
}

static void mp_free(void* ptr, size_t size) {
    // GC handles this
}

static void init_tommath() {
    if (!tommath_initialized) {
#ifdef MP_SET_MEMORY_FUNCTIONS
        mp_set_memory_functions(mp_alloc, mp_realloc, mp_free);
#endif
        tommath_initialized = true;
    }
}

TsBigInt::TsBigInt() {
    init_tommath();
    magic = 0x42494749;
    mp_init(&value);
}

TsBigInt::~TsBigInt() {
    mp_clear(&value);
}

TsBigInt* TsBigInt::Create(int64_t val) {
    TsBigInt* bi = (TsBigInt*)ts_alloc(sizeof(TsBigInt));
    new (bi) TsBigInt();
    mp_set_i64(&bi->value, val);
    return bi;
}

TsBigInt* TsBigInt::Create(const char* str, int radix) {
    TsBigInt* bi = (TsBigInt*)ts_alloc(sizeof(TsBigInt));
    new (bi) TsBigInt();
    if (mp_read_radix(&bi->value, str, radix) != MP_OKAY) {
        mp_set_i32(&bi->value, 0);
    }
    return bi;
}

const char* TsBigInt::ToString(int radix) const {
    int size;
    mp_radix_size(&value, radix, &size);
    char* str = (char*)ts_alloc(size);
    mp_to_radix(&value, str, size, NULL, radix);
    // ECMA-262 uses lowercase digits (a-z) for radices > 10; libtommath emits
    // uppercase. Lowercase in place (sign and 0-9 are untouched).
    for (char* c = str; *c; ++c)
        if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
    return str;
}

extern "C" {

void* ts_bigint_create_int(int64_t val) {
    return TsBigInt::Create(val);
}

// UNSIGNED 64-bit construction (BigUint64Array element reads: slots >= 2^63
// must box as the large positive BigInt, not a negative one).
void* ts_bigint_create_uint(uint64_t val) {
    TsBigInt* bi = (TsBigInt*)ts_alloc(sizeof(TsBigInt));
    new (bi) TsBigInt();
    mp_set_u64(&bi->value, val);
    return bi;
}

// Truncate a BigInt to a 64-bit two's-complement integer (BigInt64/BigUint64
// array element storage). ES 7.1.15/7.1.16 ToBigInt64/ToBigUint64: the value
// MODULO 2^64 — a WRAP, not saturation (mp_get_i64 clamps out-of-range
// values, so 2n**63n + 2n stored as INT64_MAX instead of wrapping negative).
// Take the low 64 magnitude bits digit-wise, then apply two's complement for
// negative values.
int64_t ts_bigint_to_i64(void* bi) {
    if (!bi) return 0;
    mp_int* v = &((TsBigInt*)bi)->value;
    uint64_t lo = 0;
    int bits = 0;
    for (int i = 0; i < v->used && bits < 64; i++) {
        lo |= ((uint64_t)v->dp[i]) << bits;
        bits += MP_DIGIT_BIT;
    }
    if (v->sign == MP_NEG) lo = ~lo + 1;
    return (int64_t)lo;
}

// Per ECMA-262 7.1.14 StringToBigInt / 6.1.6.2.3 StringIntegerLiteral:
// skip leading/trailing whitespace, detect 0x/0X/0o/0O/0b/0B radix
// prefixes (in those cases the radix is forced and signs are not
// permitted), and otherwise parse a decimal with optional `+`/`-`.
// `defaultRadix` (typically 10) is used when no prefix is present.
static TsBigInt* bigintFromTrimmedString(const char* cstr, int defaultRadix) {
    if (!cstr) return TsBigInt::Create((int64_t)0);
    auto isWs = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
               c == '\v' || c == '\f' || c == 0xA0;
    };
    const char* p = cstr;
    while (*p && isWs(static_cast<unsigned char>(*p))) ++p;
    const char* end = p + std::strlen(p);
    while (end > p && isWs(static_cast<unsigned char>(end[-1]))) --end;
    if (p == end) return TsBigInt::Create((int64_t)0);

    int parseRadix = defaultRadix > 0 ? defaultRadix : 10;
    bool negative = false;
    bool prefixed = false;
    if ((end - p) >= 2 && p[0] == '0') {
        char ch = p[1];
        if (ch == 'x' || ch == 'X') { parseRadix = 16; p += 2; prefixed = true; }
        else if (ch == 'o' || ch == 'O') { parseRadix = 8;  p += 2; prefixed = true; }
        else if (ch == 'b' || ch == 'B') { parseRadix = 2;  p += 2; prefixed = true; }
    }
    if (!prefixed && p < end && (*p == '+' || *p == '-')) {
        negative = (*p == '-');
        ++p;
    }
    if (p == end) return TsBigInt::Create((int64_t)0);

    size_t n = static_cast<size_t>(end - p);
    std::string digits(p, n);

    TsBigInt* bi = (TsBigInt*)ts_alloc(sizeof(TsBigInt));
    new (bi) TsBigInt();
    if (mp_read_radix(&bi->value, digits.c_str(), parseRadix) != MP_OKAY) {
        mp_set_i32(&bi->value, 0);
    }
    if (negative) {
        mp_neg(&bi->value, &bi->value);
    }
    return bi;
}

void* ts_bigint_create_str(void* strArg, int32_t radix) {
    TsString* tsStr = (TsString*)strArg;
    if (!tsStr) return TsBigInt::Create((int64_t)0);
    return bigintFromTrimmedString(tsStr->ToUtf8(), radix);
}

// ECMA-262 StringToBigInt with SYNTAX VALIDATION: returns nullptr on input
// the BigInt literal grammar rejects (caller throws SyntaxError), instead of
// bigintFromTrimmedString's silent 0n. Whitespace-only is 0n per spec; a bare
// sign, a bare radix prefix, a signed prefixed literal ("-0x1"), or any
// out-of-radix digit is invalid.
void* ts_bigint_from_string_checked(void* strArg) {
    TsString* tsStr = (TsString*)strArg;
    if (!tsStr) return TsBigInt::Create((int64_t)0);
    const char* cstr = tsStr->ToUtf8();
    if (!cstr) return TsBigInt::Create((int64_t)0);
    auto isWs = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
               c == '\v' || c == '\f' || c == 0xA0;
    };
    const char* p = cstr;
    while (*p && isWs(static_cast<unsigned char>(*p))) ++p;
    const char* end = p + std::strlen(p);
    while (end > p && isWs(static_cast<unsigned char>(end[-1]))) --end;
    if (p == end) return TsBigInt::Create((int64_t)0);  // "" / whitespace -> 0n

    int parseRadix = 10;
    bool negative = false;
    bool prefixed = false;
    if ((end - p) >= 2 && p[0] == '0') {
        char ch = p[1];
        if (ch == 'x' || ch == 'X') { parseRadix = 16; p += 2; prefixed = true; }
        else if (ch == 'o' || ch == 'O') { parseRadix = 8;  p += 2; prefixed = true; }
        else if (ch == 'b' || ch == 'B') { parseRadix = 2;  p += 2; prefixed = true; }
    }
    if (!prefixed && p < end && (*p == '+' || *p == '-')) {
        negative = (*p == '-');
        ++p;
    }
    if (p == end) return nullptr;  // bare sign or bare prefix
    for (const char* q = p; q < end; ++q) {
        char c = *q;
        int dig;
        if (c >= '0' && c <= '9') dig = c - '0';
        else if (c >= 'a' && c <= 'z') dig = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') dig = c - 'A' + 10;
        else return nullptr;
        if (dig >= parseRadix) return nullptr;
    }
    std::string digits(p, static_cast<size_t>(end - p));
    TsBigInt* bi = (TsBigInt*)ts_alloc(sizeof(TsBigInt));
    new (bi) TsBigInt();
    if (mp_read_radix(&bi->value, digits.c_str(), parseRadix) != MP_OKAY) {
        mp_set_i32(&bi->value, 0);
    }
    if (negative) {
        mp_neg(&bi->value, &bi->value);
    }
    return bi;
}

void* ts_bigint_to_string(void* bi, int32_t radix) {
    if (!bi) return TsString::Create("0");
    return TsString::Create(((TsBigInt*)bi)->ToString(radix));
}

// thisBigIntValue(this) per ECMA-262 21.2.3: a BigInt.prototype method requires
// `this` to be either a BigInt primitive or a BigInt wrapper object (Object(b));
// otherwise throw TypeError. `ctx` is the method receiver — it may be a raw
// pointer, a NaN-boxed primitive (number/bool/null/undefined), or a wrapper
// TsMap carrying a hidden __BigIntData slot. Returns the underlying TsBigInt*
// or throws (and returns nullptr).
static TsBigInt* requireBigIntOrThrow(void* ctx, const char* methodName) {
    if (!ctx) ctx = ts_get_call_this();
    // ts_nanbox_safe_unbox returns nullptr for NaN-boxed specials
    // (null/undefined/true/false/numbers), avoiding a wild deref below.
    void* raw = ts_nanbox_safe_unbox(ctx);
    if (raw) {
        // Bare BigInt primitive: magic 'BIGI' at offset 0.
        if (*(uint32_t*)raw == 0x42494749) {
            return (TsBigInt*)raw;
        }
        // BigInt wrapper object (Object(b)): a TsMap ("MAPS" at offset 16)
        // carrying the hidden __BigIntData slot.
        if (*(uint32_t*)((char*)raw + 16) == 0x4D415053) {
            TsValue dk; dk.type = ValueType::STRING_PTR;
            dk.ptr_val = TsString::GetInterned("__BigIntData");
            TsValue v = ((TsMap*)raw)->Get(dk);
            if (v.type == ValueType::BIGINT_PTR && v.ptr_val) {
                return (TsBigInt*)v.ptr_val;
            }
        }
    }
    char buf[128];
    snprintf(buf, sizeof(buf),
             "BigInt.prototype.%s requires that 'this' be a BigInt", methodName);
    ts_throw((TsValue*)ts_error_create_typed("TypeError", buf));
    return nullptr;
}

// BigInt.prototype.toString([radix]) — ECMA-262 21.2.3.3.
TsValue* ts_bigint_toString_native(void* ctx, int argc, TsValue** argv) {
    TsBigInt* bi = requireBigIntOrThrow(ctx, "toString");
    if (!bi) return ts_value_make_undefined();  // unreachable (threw)
    int radix = 10;
    // ES 21.2.3.3 BigInt.prototype.toString([radix]): radix undefined -> 10;
    // otherwise ToIntegerOrInfinity(radix) and a value < 2 or > 36 throws a
    // RangeError (a fractional radix truncates toward zero first).
    if (argc >= 1 && argv && argv[0]) {
        uint64_t nb = nanbox_from_tsvalue_ptr(argv[0]);
        if (!nanbox_is_undefined(nb)) {
            double r = ts_to_number(argv[0]);
            double ri;
            if (r != r) ri = 0.0;                                    // NaN -> 0
            else if (r == INFINITY || r == -INFINITY) ri = r;        // stays out of range
            else ri = (double)(int64_t)r;                            // trunc toward zero
            if (ri < 2 || ri > 36) {
                ts_throw((TsValue*)ts_error_create_typed("RangeError",
                    "toString() radix must be an integer between 2 and 36"));
                return ts_value_make_undefined();  // unreachable (threw)
            }
            radix = (int)ri;
        }
    }
    return ts_value_make_string(TsString::Create(bi->ToString(radix)));
}

// BigInt.prototype.valueOf() — ECMA-262 21.2.3.4. Returns the BigInt primitive.
TsValue* ts_bigint_valueOf_native(void* ctx, int argc, TsValue** argv) {
    TsBigInt* bi = requireBigIntOrThrow(ctx, "valueOf");
    if (!bi) return ts_value_make_undefined();  // unreachable (threw)
    return ts_value_make_bigint(bi);
}

// BigInt.prototype.toLocaleString() — ECMA-262 21.2.3.2. We approximate with the
// decimal toString (no locale formatting), matching the brand-check requirement.
TsValue* ts_bigint_toLocaleString_native(void* ctx, int argc, TsValue** argv) {
    TsBigInt* bi = requireBigIntOrThrow(ctx, "toLocaleString");
    if (!bi) return ts_value_make_undefined();  // unreachable (threw)
    return ts_value_make_string(TsString::Create(bi->ToString(10)));
}

void* ts_bigint_from_value(TsValue* val) {
    if (!val) return TsBigInt::Create((int64_t)0);

    extern void* ts_error_create_typed(const char* type, const char* message);
    // ECMA-262 21.2.1.1 BigInt(value) / 7.1.13 ToBigInt: undefined and null
    // are not coercible -> TypeError (BigInt(undefined) must NOT silently
    // yield 0n).
    uint64_t nb = nanbox_from_tsvalue_ptr(val);
    if (nanbox_is_undefined(nb) || nanbox_is_null(nb)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Cannot convert undefined or null to a BigInt"));
        return TsBigInt::Create((int64_t)0);  // unreachable
    }

    TsValue decoded = nanbox_to_tagged(val);
    if (decoded.type == ValueType::BOOLEAN) {
        // ToBigInt(true) = 1n, ToBigInt(false) = 0n.
        return TsBigInt::Create((int64_t)(decoded.b_val ? 1 : 0));
    } else if (decoded.type == ValueType::SYMBOL_PTR) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Cannot convert a Symbol value to a BigInt"));
        return TsBigInt::Create((int64_t)0);  // unreachable
    } else if (decoded.type == ValueType::NUMBER_INT) {
        return TsBigInt::Create(decoded.i_val);
    } else if (decoded.type == ValueType::NUMBER_DBL) {
        // ECMA-262 21.2.1.1.1 NumberToBigInt: a non-integral or non-finite Number
        // (1.5, NaN, Infinity) throws a RangeError instead of being truncated.
        double d = decoded.d_val;
        extern void* ts_error_create_typed(const char* type, const char* message);
        if (d != d || d > 9.2e18 || d < -9.2e18 || d != (double)(int64_t)d) {
            ts_throw((TsValue*)ts_error_create_typed("RangeError",
                "The number is not a safe integer and cannot be converted to a BigInt"));
            return TsBigInt::Create((int64_t)0);  // unreachable
        }
        return TsBigInt::Create((int64_t)d);
    } else if (decoded.type == ValueType::STRING_PTR) {
        // Delegate to the trimming/prefix-detecting helper so that
        // BigInt("0xa"), BigInt(" 10 "), BigInt("-5"), etc. follow
        // ECMA-262 StringToBigInt rather than passing the raw string
        // straight to mp_read_radix(_, _, 10).
        return bigintFromTrimmedString(((TsString*)decoded.ptr_val)->ToUtf8(), 10);
    } else if (decoded.type == ValueType::BIGINT_PTR) {
        return decoded.ptr_val;
    }

    return TsBigInt::Create((int64_t)0);
}

TsValue* ts_value_make_bigint(void* b) {
    // NaN boxing: pointer is encoded as-is (top 16 bits = 0 for valid pointers)
    return (TsValue*)(uintptr_t)b;
}

// Arithmetic operations
void* ts_bigint_add(void* a, void* b) {
    if (!a || !b) return TsBigInt::Create((int64_t)0);
    TsBigInt* result = (TsBigInt*)ts_alloc(sizeof(TsBigInt));
    new (result) TsBigInt();
    mp_add(&((TsBigInt*)a)->value, &((TsBigInt*)b)->value, &result->value);
    return result;
}

void* ts_bigint_sub(void* a, void* b) {
    if (!a || !b) return TsBigInt::Create((int64_t)0);
    TsBigInt* result = (TsBigInt*)ts_alloc(sizeof(TsBigInt));
    new (result) TsBigInt();
    mp_sub(&((TsBigInt*)a)->value, &((TsBigInt*)b)->value, &result->value);
    return result;
}

void* ts_bigint_mul(void* a, void* b) {
    if (!a || !b) return TsBigInt::Create((int64_t)0);
    TsBigInt* result = (TsBigInt*)ts_alloc(sizeof(TsBigInt));
    new (result) TsBigInt();
    mp_mul(&((TsBigInt*)a)->value, &((TsBigInt*)b)->value, &result->value);
    return result;
}

void* ts_bigint_div(void* a, void* b) {
    if (!a || !b) return TsBigInt::Create((int64_t)0);
    TsBigInt* result = (TsBigInt*)ts_alloc(sizeof(TsBigInt));
    new (result) TsBigInt();
    mp_div(&((TsBigInt*)a)->value, &((TsBigInt*)b)->value, &result->value, NULL);
    return result;
}

void* ts_bigint_mod(void* a, void* b) {
    if (!a || !b) return TsBigInt::Create((int64_t)0);
    TsBigInt* result = (TsBigInt*)ts_alloc(sizeof(TsBigInt));
    new (result) TsBigInt();
    mp_mod(&((TsBigInt*)a)->value, &((TsBigInt*)b)->value, &result->value);
    return result;
}

void* ts_bigint_neg(void* a) {
    if (!a) return TsBigInt::Create((int64_t)0);
    TsBigInt* result = (TsBigInt*)ts_alloc(sizeof(TsBigInt));
    new (result) TsBigInt();
    mp_neg(&((TsBigInt*)a)->value, &result->value);
    return result;
}

void* ts_bigint_pow(void* base, void* exp) {
    if (!base || !exp) return TsBigInt::Create((int64_t)1);
    TsBigInt* result = (TsBigInt*)ts_alloc(sizeof(TsBigInt));
    new (result) TsBigInt();

    // libtommath's mp_expt_u32 only takes uint32_t exponent
    int64_t exp_val = mp_get_i64(&((TsBigInt*)exp)->value);
    if (exp_val < 0) {
        // Negative exponent for integers results in 0 (truncated)
        mp_set_i32(&result->value, 0);
    } else if (exp_val <= UINT32_MAX) {
        mp_expt_u32(&((TsBigInt*)base)->value, (uint32_t)exp_val, &result->value);
    } else {
        // For very large exponents, would need special handling
        mp_set_i32(&result->value, 0);
    }
    return result;
}

// Comparison operations
// ToBoolean(BigInt): 0n is falsy, every other BigInt is truthy (ES 7.1.2).
bool ts_bigint_is_zero(void* a) {
    if (!a) return true;
    return mp_iszero(&((TsBigInt*)a)->value);
}

bool ts_bigint_eq(void* a, void* b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    // Per ES spec, BigInt has no negative zero — both 0n and -0n are
    // the same value. libtommath's mp_cmp can return MP_GT/MP_LT for
    // a "+0 vs -0" pair if the sign bits differ, so check for zero
    // first and treat both zeros as equal.
    mp_int* av = &((TsBigInt*)a)->value;
    mp_int* bv = &((TsBigInt*)b)->value;
    if (mp_iszero(av) && mp_iszero(bv)) return true;
    return mp_cmp(av, bv) == MP_EQ;
}

bool ts_bigint_ne(void* a, void* b) {
    return !ts_bigint_eq(a, b);
}

bool ts_bigint_lt(void* a, void* b) {
    if (!a || !b) return false;
    return mp_cmp(&((TsBigInt*)a)->value, &((TsBigInt*)b)->value) == MP_LT;
}

bool ts_bigint_gt(void* a, void* b) {
    if (!a || !b) return false;
    return mp_cmp(&((TsBigInt*)a)->value, &((TsBigInt*)b)->value) == MP_GT;
}

bool ts_bigint_le(void* a, void* b) {
    if (!a || !b) return false;
    mp_ord cmp = mp_cmp(&((TsBigInt*)a)->value, &((TsBigInt*)b)->value);
    return cmp == MP_LT || cmp == MP_EQ;
}

bool ts_bigint_ge(void* a, void* b) {
    if (!a || !b) return false;
    mp_ord cmp = mp_cmp(&((TsBigInt*)a)->value, &((TsBigInt*)b)->value);
    return cmp == MP_GT || cmp == MP_EQ;
}

// Bitwise operations
void* ts_bigint_and(void* a, void* b) {
    if (!a || !b) return TsBigInt::Create((int64_t)0);
    TsBigInt* result = (TsBigInt*)ts_alloc(sizeof(TsBigInt));
    new (result) TsBigInt();
    mp_and(&((TsBigInt*)a)->value, &((TsBigInt*)b)->value, &result->value);
    return result;
}

void* ts_bigint_or(void* a, void* b) {
    if (!a || !b) return TsBigInt::Create((int64_t)0);
    TsBigInt* result = (TsBigInt*)ts_alloc(sizeof(TsBigInt));
    new (result) TsBigInt();
    mp_or(&((TsBigInt*)a)->value, &((TsBigInt*)b)->value, &result->value);
    return result;
}

void* ts_bigint_xor(void* a, void* b) {
    if (!a || !b) return TsBigInt::Create((int64_t)0);
    TsBigInt* result = (TsBigInt*)ts_alloc(sizeof(TsBigInt));
    new (result) TsBigInt();
    mp_xor(&((TsBigInt*)a)->value, &((TsBigInt*)b)->value, &result->value);
    return result;
}

void* ts_bigint_not(void* a) {
    if (!a) return TsBigInt::Create((int64_t)-1);
    TsBigInt* result = (TsBigInt*)ts_alloc(sizeof(TsBigInt));
    new (result) TsBigInt();
    // Bitwise NOT: ~x = -x - 1 (two's complement)
    mp_neg(&((TsBigInt*)a)->value, &result->value);
    mp_sub_d(&result->value, 1, &result->value);
    return result;
}

void* ts_bigint_shl(void* a, int64_t bits) {
    if (!a) return TsBigInt::Create((int64_t)0);
    TsBigInt* result = (TsBigInt*)ts_alloc(sizeof(TsBigInt));
    new (result) TsBigInt();
    mp_mul_2d(&((TsBigInt*)a)->value, (int)bits, &result->value);
    return result;
}

void* ts_bigint_shr(void* a, int64_t bits) {
    if (!a) return TsBigInt::Create((int64_t)0);
    TsBigInt* result = (TsBigInt*)ts_alloc(sizeof(TsBigInt));
    new (result) TsBigInt();
    mp_div_2d(&((TsBigInt*)a)->value, (int)bits, &result->value, NULL);
    return result;
}

}
