// Test: is-number - check if a value is a number
// Exercises: Pure function export, type checking, edge cases (NaN, Infinity, null, undefined)

import isNumber from 'is-number';

function user_main(): number {
    let failures = 0;

    // Test 1: Numeric value
    if (isNumber(123) === true) {
        console.log("PASS: isNumber(123) === true");
    } else {
        console.log("FAIL: isNumber(123) expected true, got " + isNumber(123));
        failures++;
    }

    // Test 2: Numeric string
    if (isNumber('123') === true) {
        console.log("PASS: isNumber('123') === true");
    } else {
        console.log("FAIL: isNumber('123') expected true, got " + isNumber('123'));
        failures++;
    }

    // Test 3: Non-numeric string
    if (isNumber('abc') === false) {
        console.log("PASS: isNumber('abc') === false");
    } else {
        console.log("FAIL: isNumber('abc') expected false, got " + isNumber('abc'));
        failures++;
    }

    // Test 4: NaN
    if (isNumber(NaN) === false) {
        console.log("PASS: isNumber(NaN) === false");
    } else {
        console.log("FAIL: isNumber(NaN) expected false, got " + isNumber(NaN));
        failures++;
    }

    // Test 5: Infinity (Infinity - Infinity === NaN, so isNumber returns false)
    if (isNumber(Infinity) === false) {
        console.log("PASS: isNumber(Infinity) === false");
    } else {
        console.log("FAIL: isNumber(Infinity) expected false, got " + isNumber(Infinity));
        failures++;
    }

    // Test 6: null
    if (isNumber(null) === false) {
        console.log("PASS: isNumber(null) === false");
    } else {
        console.log("FAIL: isNumber(null) expected false, got " + isNumber(null));
        failures++;
    }

    // Test 7: undefined
    if (isNumber(undefined) === false) {
        console.log("PASS: isNumber(undefined) === false");
    } else {
        console.log("FAIL: isNumber(undefined) expected false, got " + isNumber(undefined));
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All is-number tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    process.exit(failures);
    return failures;
}
