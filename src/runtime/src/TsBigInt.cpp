#include "TsBigInt.h"
#include "TsRuntime.h"
#include "TsString.h"
#include <gc/gc.h>
#include <cstring>

static bool tommath_initialized = false;

static void* mp_alloc(size_t size) {
    return GC_malloc(size);
}

static void* mp_realloc(void* ptr, size_t old_size, size_t new_size) {
    return GC_realloc(ptr, new_size);
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
    char* str = (char*)GC_malloc_atomic(size);
    mp_to_radix(&value, str, size, NULL, radix);
    return str;
}

extern "C" {

void* ts_bigint_create_int(int64_t val) {
    return TsBigInt::Create(val);
}

void* ts_bigint_create_str(void* strArg, int32_t radix) {
    // strArg is a TsString* from the HIR const.string
    TsString* tsStr = (TsString*)strArg;
    if (!tsStr) return TsBigInt::Create((int64_t)0);
    const char* cstr = tsStr->ToUtf8();
    return TsBigInt::Create(cstr, radix);
}

void* ts_bigint_to_string(void* bi, int32_t radix) {
    if (!bi) return TsString::Create("0");
    return TsString::Create(((TsBigInt*)bi)->ToString(radix));
}

void* ts_bigint_from_value(TsValue* val) {
    if (!val) return TsBigInt::Create((int64_t)0);
    
    if (val->type == ValueType::NUMBER_INT) {
        return TsBigInt::Create(val->i_val);
    } else if (val->type == ValueType::NUMBER_DBL) {
        return TsBigInt::Create((int64_t)val->d_val);
    } else if (val->type == ValueType::STRING_PTR) {
        return TsBigInt::Create(((TsString*)val->ptr_val)->ToUtf8());
    } else if (val->type == ValueType::BIGINT_PTR) {
        return val->ptr_val;
    }
    
    return TsBigInt::Create((int64_t)0);
}

TsValue* ts_value_make_bigint(void* b) {
    TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
    v->type = ValueType::BIGINT_PTR;
    v->ptr_val = b;
    return v;
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
bool ts_bigint_eq(void* a, void* b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    return mp_cmp(&((TsBigInt*)a)->value, &((TsBigInt*)b)->value) == MP_EQ;
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
