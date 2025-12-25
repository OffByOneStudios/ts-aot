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

void* ts_bigint_create_str(const char* str, int32_t radix) {
    return TsBigInt::Create(str, radix);
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

}
