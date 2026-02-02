// TsAssert.h - Assert module for ts-aot
//
// Node.js-compatible assertion functions.
// This module is built as a separate library (ts_assert) and linked
// only when the assert module is imported.

#ifndef TS_ASSERT_EXT_H
#define TS_ASSERT_EXT_H

#ifdef __cplusplus
extern "C" {
#endif

// Basic assertions
void ts_assert_ok(void* value, void* message);
void ts_assert_equal(void* actual, void* expected, void* message);
void ts_assert_not_equal(void* actual, void* expected, void* message);
void ts_assert_strict_equal(void* actual, void* expected, void* message);
void ts_assert_not_strict_equal(void* actual, void* expected, void* message);

// Deep equality assertions
void ts_assert_deep_equal(void* actual, void* expected, void* message);
void ts_assert_not_deep_equal(void* actual, void* expected, void* message);
void ts_assert_deep_strict_equal(void* actual, void* expected, void* message);
void ts_assert_not_deep_strict_equal(void* actual, void* expected, void* message);

// Exception assertions
void ts_assert_throws(void* fn, void* error, void* message);
void ts_assert_does_not_throw(void* fn, void* error, void* message);
void* ts_assert_rejects(void* asyncFn, void* error, void* message);
void* ts_assert_does_not_reject(void* asyncFn, void* error, void* message);

// Other assertions
void ts_assert_fail(void* message);
void ts_assert_if_error(void* value);
void ts_assert_match(void* str, void* regexp, void* message);
void ts_assert_does_not_match(void* str, void* regexp, void* message);

#ifdef __cplusplus
}
#endif

#endif // TS_ASSERT_EXT_H
