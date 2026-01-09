// Test Object.is() static method

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Same primitive values
    if (Object.is(1, 1)) {
        console.log("PASS: Object.is(1, 1) === true");
        passed++;
    } else {
        console.log("FAIL: Object.is(1, 1) should be true");
        failed++;
    }

    // Test 2: Different primitive values
    if (!Object.is(1, 2)) {
        console.log("PASS: Object.is(1, 2) === false");
        passed++;
    } else {
        console.log("FAIL: Object.is(1, 2) should be false");
        failed++;
    }

    // Test 3: Same string values
    if (Object.is("hello", "hello")) {
        console.log("PASS: Object.is('hello', 'hello') === true");
        passed++;
    } else {
        console.log("FAIL: Object.is('hello', 'hello') should be true");
        failed++;
    }

    // Test 4: Different string values
    if (!Object.is("hello", "world")) {
        console.log("PASS: Object.is('hello', 'world') === false");
        passed++;
    } else {
        console.log("FAIL: Object.is('hello', 'world') should be false");
        failed++;
    }

    // Test 5: NaN equality (different from ===)
    if (Object.is(NaN, NaN)) {
        console.log("PASS: Object.is(NaN, NaN) === true");
        passed++;
    } else {
        console.log("FAIL: Object.is(NaN, NaN) should be true");
        failed++;
    }

    // Test 6: +0 and -0 are different (different from ===)
    if (!Object.is(0, -0)) {
        console.log("PASS: Object.is(0, -0) === false");
        passed++;
    } else {
        console.log("FAIL: Object.is(0, -0) should be false");
        failed++;
    }

    // Test 7: -0 equals -0
    if (Object.is(-0, -0)) {
        console.log("PASS: Object.is(-0, -0) === true");
        passed++;
    } else {
        console.log("FAIL: Object.is(-0, -0) should be true");
        failed++;
    }

    // Test 8: +0 equals +0
    if (Object.is(0, 0)) {
        console.log("PASS: Object.is(0, 0) === true");
        passed++;
    } else {
        console.log("FAIL: Object.is(0, 0) should be true");
        failed++;
    }

    // Test 9: Boolean true
    if (Object.is(true, true)) {
        console.log("PASS: Object.is(true, true) === true");
        passed++;
    } else {
        console.log("FAIL: Object.is(true, true) should be true");
        failed++;
    }

    // Test 10: Boolean false
    if (Object.is(false, false)) {
        console.log("PASS: Object.is(false, false) === true");
        passed++;
    } else {
        console.log("FAIL: Object.is(false, false) should be true");
        failed++;
    }

    // Test 11: Different types
    if (!Object.is(1, "1")) {
        console.log("PASS: Object.is(1, '1') === false");
        passed++;
    } else {
        console.log("FAIL: Object.is(1, '1') should be false");
        failed++;
    }

    // Test 12: null equality
    if (Object.is(null, null)) {
        console.log("PASS: Object.is(null, null) === true");
        passed++;
    } else {
        console.log("FAIL: Object.is(null, null) should be true");
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
