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
    void* ts_bigint_create_str(const char* str, int32_t radix);
    void* ts_bigint_to_string(void* bi, int32_t radix);
    void* ts_bigint_from_value(TsValue* val);
}
