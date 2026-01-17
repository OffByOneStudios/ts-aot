#pragma once

#include "TsObject.h"
#include "TsString.h"

extern "C" {
    // Basic assertions
    void ts_assert(void* value, void* message);
    void ts_assert_ok(void* value, void* message);

    // Equality assertions
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

    // Utility assertions
    void ts_assert_fail(void* message);
    void ts_assert_if_error(void* value);
    void ts_assert_match(void* str, void* regexp, void* message);
    void ts_assert_does_not_match(void* str, void* regexp, void* message);
}
