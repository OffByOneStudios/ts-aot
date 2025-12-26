#pragma once
#include <cstdint>
#include <cstddef>

class TsString;

enum class ValueType : uint8_t {
    UNDEFINED = 0,
    NUMBER_INT,
    NUMBER_DBL,
    BOOLEAN,
    STRING_PTR,
    OBJECT_PTR,
    PROMISE_PTR,
    ARRAY_PTR,
    BIGINT_PTR,
    SYMBOL_PTR
};

struct TaggedValue {
    ValueType type;
    union {
        int64_t i_val;
        double d_val;
        bool b_val;
        void* ptr_val;
    };

    TaggedValue() : type(ValueType::UNDEFINED), ptr_val(nullptr) {}
    TaggedValue(std::nullptr_t) : type(ValueType::UNDEFINED), ptr_val(nullptr) {}
    TaggedValue(int64_t v) : type(ValueType::NUMBER_INT), i_val(v) {}
    TaggedValue(double v) : type(ValueType::NUMBER_DBL), d_val(v) {}
    TaggedValue(bool v) : type(ValueType::BOOLEAN), b_val(v) {}
    TaggedValue(void* v) : type(ValueType::OBJECT_PTR), ptr_val(v) {}
    TaggedValue(TsString* v) : type(ValueType::STRING_PTR), ptr_val(v) {}
    
    operator void*() const { return (type == ValueType::OBJECT_PTR || type == ValueType::STRING_PTR) ? ptr_val : nullptr; }
};

using TsValue = TaggedValue;

class TsObject {
public:
    void* vtable;
};

enum class FunctionType {
    COMPILED,
    NATIVE
};

class TsFunction : public TsObject {
public:
    void* funcPtr;
    void* context;
    FunctionType type;
    TsFunction(void* fp, void* ctx = nullptr, FunctionType t = FunctionType::COMPILED) 
        : funcPtr(fp), context(ctx), type(t) {}
};

typedef TaggedValue* (*TsFunctionPtr)(void* context, int argc, TaggedValue** argv);
typedef TaggedValue* (*TsFunctionPtrNoArgs)(void* context);

extern "C" {
    TsValue* ts_value_make_int(int64_t v);
    TsValue* ts_value_make_double(double v);
    TsValue* ts_value_make_bool(bool v);
    TsValue* ts_value_make_string(void* str);
    TsValue* ts_value_make_object(void* obj);
    TsValue* ts_value_make_promise(void* promise);
    TsValue* ts_value_make_array(void* arr);
    TsValue* ts_value_make_function(void* funcPtr, void* context);
    TsValue* ts_value_make_native_function(void* funcPtr, void* context);
    TsValue* ts_call_0(TsValue* boxedFunc);
    TsValue* ts_call_1(TsValue* boxedFunc, TsValue* arg1);
    TsValue* ts_call_2(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2);
    TsValue* ts_call_3(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3);
    TsValue* ts_function_call(TsValue* boxedFunc, int argc, TsValue** argv);
}
