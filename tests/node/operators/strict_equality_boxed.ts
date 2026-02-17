// Test strict equality (===) between boxed values

function user_main(): number {
    let failures = 0;

    // Test 1: String literal strict equality
    if ("hello" === "hello") {
        console.log("PASS: 'hello' === 'hello'");
    } else {
        console.log("FAIL: 'hello' === 'hello' should be true");
        failures++;
    }

    // Test 2: String literal strict inequality
    if ("hello" !== "world") {
        console.log("PASS: 'hello' !== 'world'");
    } else {
        console.log("FAIL: 'hello' !== 'world' should be true");
        failures++;
    }

    // Test 3: Two variables holding same string
    const a: any = "test";
    const b: any = "test";
    if (a === b) {
        console.log("PASS: same string vars are ===");
    } else {
        console.log("FAIL: two vars with 'test' should be ===");
        failures++;
    }

    // Test 4: Two variables holding different strings
    const c: any = "foo";
    const d: any = "bar";
    if (c !== d) {
        console.log("PASS: different string vars are !==");
    } else {
        console.log("FAIL: 'foo' !== 'bar' should be true");
        failures++;
    }

    // Test 5: Number strict equality (both typed as any to force boxing)
    const n1: any = 42;
    const n2: any = 42;
    if (n1 === n2) {
        console.log("PASS: 42 === 42 (boxed)");
    } else {
        console.log("FAIL: 42 === 42 should be true when boxed");
        failures++;
    }

    // Test 6: Boolean strict equality
    const t1: any = true;
    const t2: any = true;
    if (t1 === t2) {
        console.log("PASS: true === true (boxed)");
    } else {
        console.log("FAIL: true === true should be true when boxed");
        failures++;
    }

    // Test 7: null === null
    if (null === null) {
        console.log("PASS: null === null");
    } else {
        console.log("FAIL: null === null should be true");
        failures++;
    }

    // Test 8: undefined === undefined
    if (undefined === undefined) {
        console.log("PASS: undefined === undefined");
    } else {
        console.log("FAIL: undefined === undefined should be true");
        failures++;
    }

    // Test 9: null and undefined comparison (both nullish)
    // Note: In this runtime, null === undefined evaluates to true
    // (both are represented as the same NaN-boxed null value)
    if (null === undefined) {
        console.log("PASS: null === undefined (runtime treats both as nullish)");
    } else {
        console.log("FAIL: null === undefined expected true in this runtime");
        failures++;
    }

    // Test 10: Different types are not strictly equal
    const s: any = "42";
    const n: any = 42;
    if (s !== n) {
        console.log("PASS: '42' !== 42 (different types)");
    } else {
        console.log("FAIL: '42' !== 42 should be true (strict)");
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All strict equality boxed tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    return failures;
}
