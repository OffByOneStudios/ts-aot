// Test Date.now() and static methods

function user_main(): number {
    let failures = 0;

    // Test 1: Date.now() returns a number > 0
    const t1 = Date.now();
    if (t1 > 0) {
        console.log("PASS: Date.now() > 0");
    } else {
        console.log("FAIL: Date.now() expected > 0, got " + t1);
        failures++;
    }

    // Test 2: Date.now() returns increasing values
    const t2 = Date.now();
    if (t2 >= t1) {
        console.log("PASS: Date.now() is non-decreasing");
    } else {
        console.log("FAIL: second Date.now() < first: " + t2 + " < " + t1);
        failures++;
    }

    // Test 3: typeof Date.now() === "number"
    if (typeof t1 === "number") {
        console.log("PASS: typeof Date.now() === 'number'");
    } else {
        console.log("FAIL: typeof Date.now() expected 'number', got '" + typeof t1 + "'");
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All date static tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    return failures;
}
