#pragma once

#include "TsObject.h"
#include "TsString.h"
#include <cstdint>

// Boxed primitive wrapper classes for JavaScript's Boolean, Number, String objects
// These are created by `new Boolean(value)`, `new Number(value)`, `new String(value)`

class TsBooleanObject : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x424F4F4C; // "BOOL"
    bool value;

    TsBooleanObject(bool v) : value(v) {
        vtable = nullptr;
        magic = MAGIC;
    }

    static TsBooleanObject* Create(bool value);

    bool GetValue() const { return value; }
};

class TsNumberObject : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x4E554D42; // "NUMB"
    double value;

    TsNumberObject(double v) : value(v) {
        vtable = nullptr;
        magic = MAGIC;
    }

    static TsNumberObject* Create(double value);

    double GetValue() const { return value; }
};

class TsStringObject : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x53545257; // "STRW" (String Wrapper)
    TsString* value;

    TsStringObject(TsString* v) : value(v) {
        vtable = nullptr;
        magic = MAGIC;
    }

    static TsStringObject* Create(TsString* value);

    TsString* GetValue() const { return value; }
};

// Forward declaration for TsSymbol
class TsSymbol;

class TsSymbolObject : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x53594D4F; // "SYMO" (Symbol Object)
    TsSymbol* value;

    TsSymbolObject(TsSymbol* v) : value(v) {
        vtable = nullptr;
        magic = MAGIC;
    }

    static TsSymbolObject* Create(TsSymbol* value);

    TsSymbol* GetValue() const { return value; }
};

extern "C" {
    // Create boxed primitive objects
    void* ts_boolean_object_create(bool value);
    void* ts_number_object_create(double value);
    void* ts_string_object_create(void* strValue);
    void* ts_symbol_object_create(void* symValue);

    // Get primitive values from boxed objects
    bool ts_boolean_object_value(void* obj);
    double ts_number_object_value(void* obj);
    void* ts_string_object_value(void* obj);
    void* ts_symbol_object_value(void* obj);
}
