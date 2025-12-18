#pragma once
#include <cstdint>

enum class ValueType : uint8_t {
    UNDEFINED = 0,
    NUMBER_INT,
    NUMBER_DBL,
    BOOLEAN,
    STRING_PTR,
    OBJECT_PTR
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
    
    operator void*() const { return (type == ValueType::OBJECT_PTR || type == ValueType::STRING_PTR) ? ptr_val : nullptr; }
};

using TsValue = TaggedValue;

class TsObject {
public:
    virtual ~TsObject() = default;
};
