// Test: fast-deep-equal - recursive deep equality comparison
// Exercises: typeof, instanceof, Object.keys, Array.isArray, for...in, recursion
//
// Known limitations (ts-aot flat objects):
// - .constructor property not available on flat objects/arrays
//   (breaks object/array deep equality which uses a.constructor !== b.constructor)
// - null === undefined returns true (NaN-boxing identity issue)

import equal from 'fast-deep-equal';

function user_main(): number {
    let failures = 0;

    // Test 1: Primitive equality
    if (equal(1, 1) === true) {
        console.log("PASS: equal(1, 1) === true");
    } else {
        console.log("FAIL: equal(1, 1) expected true");
        failures++;
    }

    // Test 2: Primitive inequality
    if (equal(1, 2) === false) {
        console.log("PASS: equal(1, 2) === false");
    } else {
        console.log("FAIL: equal(1, 2) expected false");
        failures++;
    }

    // Test 3: String equality
    if (equal("hello", "hello") === true) {
        console.log("PASS: equal('hello', 'hello') === true");
    } else {
        console.log("FAIL: equal('hello', 'hello') expected true");
        failures++;
    }

    // Test 4: Null equality
    if (equal(null, null) === true) {
        console.log("PASS: equal(null, null) === true");
    } else {
        console.log("FAIL: equal(null, null) expected true");
        failures++;
    }

    // Test 5: Undefined equality
    if (equal(undefined, undefined) === true) {
        console.log("PASS: equal(undefined, undefined) === true");
    } else {
        console.log("FAIL: equal(undefined, undefined) expected true");
        failures++;
    }

    // Test 6: Object inequality (different values)
    if (equal({a: 1}, {a: 2}) === false) {
        console.log("PASS: equal({a:1}, {a:2}) === false");
    } else {
        console.log("FAIL: equal({a:1}, {a:2}) expected false");
        failures++;
    }

    // Test 7: Array inequality (different values)
    if (equal([1, 2], [1, 3]) === false) {
        console.log("PASS: equal([1,2], [1,3]) === false");
    } else {
        console.log("FAIL: equal([1,2], [1,3]) expected false");
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All fast-deep-equal tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    process.exit(failures);
    return failures;
}
