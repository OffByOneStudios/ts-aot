// Stub implementations for test extension (extensions/test/test.ext.json)
// These are minimal stubs to allow linking - they return dummy values

#include "../include/TsString.h"
#include "../include/TsObject.h"

extern "C" {

// Runtime function implementations (called by lowering)
int64_t ts_test_get_value(void* self) {
    (void)self;
    return 42;  // Stub value
}

void* ts_test_get_name(void* self) {
    (void)self;
    return TsString::Create("TestInstance");
}

int64_t ts_test_add(void* self, int64_t a, int64_t b) {
    (void)self;
    return a + b;
}

// VTable method implementations (wrapper format expected by codegen)
// These follow the pattern: ClassName_methodName
void* TestClass_getValue(void* self) {
    return ts_value_make_int(ts_test_get_value(self));
}

void* TestClass_getName(void* self) {
    return ts_value_make_string(ts_test_get_name(self));
}

void* TestClass_add(void* self, void* a, void* b) {
    int64_t aVal = ts_value_get_int((TsValue*)a);
    int64_t bVal = ts_value_get_int((TsValue*)b);
    return ts_value_make_int(ts_test_add(self, aVal, bVal));
}

// Property getters
void* TestClass_count(void* self) {
    (void)self;
    return ts_value_make_int(100);  // Stub value
}

void* TestClass_label(void* self) {
    (void)self;
    return ts_value_make_string(TsString::Create("test-label"));
}

}  // extern "C"
