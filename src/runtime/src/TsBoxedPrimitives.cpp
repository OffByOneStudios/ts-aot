#include "../include/TsBoxedPrimitives.h"
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

bool ts_boolean_object_value(void* obj) {
    TsBooleanObject* boolObj = dynamic_cast<TsBooleanObject*>((TsObject*)obj);
    if (boolObj) {
        return boolObj->GetValue();
    }
    return false;
}

double ts_number_object_value(void* obj) {
    TsNumberObject* numObj = dynamic_cast<TsNumberObject*>((TsObject*)obj);
    if (numObj) {
        return numObj->GetValue();
    }
    return 0.0;
}

void* ts_string_object_value(void* obj) {
    TsStringObject* strObj = dynamic_cast<TsStringObject*>((TsObject*)obj);
    if (strObj) {
        return strObj->GetValue();
    }
    return nullptr;
}

} // extern "C"
