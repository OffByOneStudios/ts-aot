#include "TsObject.h"
#include "TsRuntime.h"
#include <new>

// Currently empty, as TsObject.h only defines structs/enums for now.

extern "C" {

TsValue* ts_value_make_undefined() {
    TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
    v->type = ValueType::UNDEFINED;
    v->ptr_val = nullptr;
    return v;
}

TsValue* ts_value_make_int(int64_t i) {
    TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
    v->type = ValueType::NUMBER_INT;
    v->i_val = i;
    return v;
}

TsValue* ts_value_make_double(double d) {
    TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
    v->type = ValueType::NUMBER_DBL;
    v->d_val = d;
    return v;
}

TsValue* ts_value_make_bool(bool b) {
    TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
    v->type = ValueType::BOOLEAN;
    v->b_val = b;
    return v;
}

TsValue* ts_value_make_string(void* s) {
    TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
    v->type = ValueType::STRING_PTR;
    v->ptr_val = s;
    return v;
}

TsValue* ts_value_make_object(void* o) {
    TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
    v->type = ValueType::OBJECT_PTR;
    v->ptr_val = o;
    return v;
}

TsValue* ts_value_make_function(void* f, void* ctx) {
    TsFunction* func = new (ts_alloc(sizeof(TsFunction))) TsFunction(f, ctx);
    TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
    v->type = ValueType::OBJECT_PTR;
    v->ptr_val = func;
    return v;
}

void* ts_function_get_ptr(TsValue* val) {
    if (!val || val->type != ValueType::OBJECT_PTR) return nullptr;
    TsFunction* func = (TsFunction*)val->ptr_val;
    return func->funcPtr;
}

int64_t ts_value_get_int(TsValue* v) {
    if (!v) return 0;
    if (v->type == ValueType::NUMBER_INT) return v->i_val;
    if (v->type == ValueType::NUMBER_DBL) return (int64_t)v->d_val;
    return 0;
}

double ts_value_get_double(TsValue* v) {
    if (!v) return 0.0;
    if (v->type == ValueType::NUMBER_DBL) return v->d_val;
    if (v->type == ValueType::NUMBER_INT) return (double)v->i_val;
    return 0.0;
}

bool ts_value_get_bool(TsValue* v) {
    if (!v) return false;
    if (v->type == ValueType::BOOLEAN) return v->b_val;
    return false;
}

void* ts_value_get_string(TsValue* v) {
    if (!v) return nullptr;
    if (v->type == ValueType::STRING_PTR) return v->ptr_val;
    return nullptr;
}

void* ts_string_from_value(TsValue* v) {
    if (!v) return ts_string_create("undefined");
    if (v->type == ValueType::STRING_PTR) return v->ptr_val;
    if (v->type == ValueType::NUMBER_INT) return ts_string_from_int(v->i_val);
    if (v->type == ValueType::NUMBER_DBL) return ts_string_from_double(v->d_val);
    if (v->type == ValueType::BOOLEAN) return ts_string_from_bool(v->b_val);
    return ts_string_create("[object Object]");
}

}
