#include "../include/TsBoxedPrimitives.h"
#include "../include/TsSymbol.h"
#include "../include/TsFlatObject.h"
#include "../include/GC.h"

// TsBooleanObject implementation
TsBooleanObject* TsBooleanObject::Create(bool value) {
    void* mem = ts_alloc(sizeof(TsBooleanObject));
    return new (mem) TsBooleanObject(value);
}

// TsNumberObject implementation
TsNumberObject* TsNumberObject::Create(double value) {
    void* mem = ts_alloc(sizeof(TsNumberObject));
    return new (mem) TsNumberObject(value);
}

// TsStringObject implementation
TsStringObject* TsStringObject::Create(TsString* value) {
    void* mem = ts_alloc(sizeof(TsStringObject));
    return new (mem) TsStringObject(value);
}

// TsSymbolObject implementation
TsSymbolObject* TsSymbolObject::Create(TsSymbol* value) {
    void* mem = ts_alloc(sizeof(TsSymbolObject));
    return new (mem) TsSymbolObject(value);
}

extern "C" {

void* ts_boolean_object_create(bool value) {
    return TsBooleanObject::Create(value);
}

void* ts_number_object_create(double value) {
    return TsNumberObject::Create(value);
}

void* ts_string_object_create(void* strValue) {
    return TsStringObject::Create((TsString*)strValue);
}

void* ts_symbol_object_create(void* symValue) {
    return TsSymbolObject::Create((TsSymbol*)symValue);
}

bool ts_boolean_object_value(void* obj) {
    if (!obj || is_flat_object(obj)) return false;
    TsBooleanObject* boolObj = dynamic_cast<TsBooleanObject*>((TsObject*)obj);
    if (boolObj) {
        return boolObj->GetValue();
    }
    return false;
}

double ts_number_object_value(void* obj) {
    if (!obj || is_flat_object(obj)) return 0.0;
    TsNumberObject* numObj = dynamic_cast<TsNumberObject*>((TsObject*)obj);
    if (numObj) {
        return numObj->GetValue();
    }
    return 0.0;
}

void* ts_string_object_value(void* obj) {
    if (!obj || is_flat_object(obj)) return nullptr;
    TsStringObject* strObj = dynamic_cast<TsStringObject*>((TsObject*)obj);
    if (strObj) {
        return strObj->GetValue();
    }
    return nullptr;
}

void* ts_symbol_object_value(void* obj) {
    if (!obj || is_flat_object(obj)) return nullptr;
    TsSymbolObject* symObj = dynamic_cast<TsSymbolObject*>((TsObject*)obj);
    if (symObj) {
        return symObj->GetValue();
    }
    return nullptr;
}

} // extern "C"
