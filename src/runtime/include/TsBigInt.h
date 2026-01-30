#pragma once
#include <tommath.h>
#include "TsObject.h"

class TsBigInt {
public:
    uint32_t magic; // 0x42494749 'BIGI'
    mp_int value;

    static TsBigInt* Create(int64_t val);
    static TsBigInt* Create(const char* str, int radix = 10);
    
    const char* ToString(int radix = 10) const;

    TsBigInt();
    ~TsBigInt();
};

extern "C" {
    void* ts_bigint_create_int(int64_t val);
    void* ts_bigint_create_str(void* tsStr, int32_t radix);
    void* ts_bigint_to_string(void* bi, int32_t radix);
    void* ts_bigint_from_value(TsValue* val);

    // Arithmetic operations
    void* ts_bigint_add(void* a, void* b);
    void* ts_bigint_sub(void* a, void* b);
    void* ts_bigint_mul(void* a, void* b);
    void* ts_bigint_div(void* a, void* b);
    void* ts_bigint_mod(void* a, void* b);
    void* ts_bigint_neg(void* a);
    void* ts_bigint_pow(void* base, void* exp);

    // Comparison
    bool ts_bigint_eq(void* a, void* b);
    bool ts_bigint_lt(void* a, void* b);
    bool ts_bigint_gt(void* a, void* b);
    bool ts_bigint_le(void* a, void* b);
    bool ts_bigint_ge(void* a, void* b);

    // Bitwise operations
    void* ts_bigint_and(void* a, void* b);
    void* ts_bigint_or(void* a, void* b);
    void* ts_bigint_xor(void* a, void* b);
    void* ts_bigint_not(void* a);
    void* ts_bigint_shl(void* a, int64_t bits);
    void* ts_bigint_shr(void* a, int64_t bits);
}
