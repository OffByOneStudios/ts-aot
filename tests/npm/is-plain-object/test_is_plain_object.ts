// Test: is-plain-object - check if a value is a plain object
// Exercises: Object type checking, prototype chain inspection, named export

import { isPlainObject } from 'is-plain-object';

function user_main(): number {
    let failures = 0;

    // Test 1: Empty object literal
    if (isPlainObject({}) === true) {
        console.log("PASS: isPlainObject({}) === true");
    } else {
        console.log("FAIL: isPlainObject({}) expected true, got " + isPlainObject({}));
        failures++;
    }

    // Test 2: new Object()
    if (isPlainObject(new Object()) === true) {
        console.log("PASS: isPlainObject(new Object()) === true");
    } else {
        console.log("FAIL: isPlainObject(new Object()) expected true, got " + isPlainObject(new Object()));
        failures++;
    }

    // Test 3: Array is not a plain object
    if (isPlainObject([]) === false) {
        console.log("PASS: isPlainObject([]) === false");
    } else {
        console.log("FAIL: isPlainObject([]) expected false, got " + isPlainObject([]));
        failures++;
    }

    // Test 4: null is not a plain object
    if (isPlainObject(null) === false) {
        console.log("PASS: isPlainObject(null) === false");
    } else {
        console.log("FAIL: isPlainObject(null) expected false, got " + isPlainObject(null));
        failures++;
    }

    // Test 5: Number is not a plain object
    if (isPlainObject(42) === false) {
        console.log("PASS: isPlainObject(42) === false");
    } else {
        console.log("FAIL: isPlainObject(42) expected false, got " + isPlainObject(42));
        failures++;
    }

    // Test 6: String is not a plain object
    if (isPlainObject('string') === false) {
        console.log("PASS: isPlainObject('string') === false");
    } else {
        console.log("FAIL: isPlainObject('string') expected false, got " + isPlainObject('string'));
        failures++;
    }

    // Test 7: Object.create(null) is a plain object
    if (isPlainObject(Object.create(null)) === true) {
        console.log("PASS: isPlainObject(Object.create(null)) === true");
    } else {
        console.log("FAIL: isPlainObject(Object.create(null)) expected true, got " + isPlainObject(Object.create(null)));
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All is-plain-object tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    process.exit(failures);
    return failures;
}
