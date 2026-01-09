// Test Number static methods: isFinite, isNaN, isInteger, isSafeInteger

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // ==================== Number.isFinite() ====================

    // Test 1: Normal numbers are finite
    if (Number.isFinite(42)) {
        console.log("PASS: Number.isFinite(42) = true");
        passed++;
    } else {
        console.log("FAIL: Number.isFinite(42) should be true");
        failed++;
    }

    // Test 2: Floating point numbers are finite
    if (Number.isFinite(3.14)) {
        console.log("PASS: Number.isFinite(3.14) = true");
        passed++;
    } else {
        console.log("FAIL: Number.isFinite(3.14) should be true");
        failed++;
    }

    // Test 3: Infinity is not finite
    if (!Number.isFinite(Infinity)) {
        console.log("PASS: Number.isFinite(Infinity) = false");
        passed++;
    } else {
        console.log("FAIL: Number.isFinite(Infinity) should be false");
        failed++;
    }

    // Test 4: Negative infinity is not finite
    if (!Number.isFinite(-Infinity)) {
        console.log("PASS: Number.isFinite(-Infinity) = false");
        passed++;
    } else {
        console.log("FAIL: Number.isFinite(-Infinity) should be false");
        failed++;
    }

    // Test 5: NaN is not finite
    if (!Number.isFinite(NaN)) {
        console.log("PASS: Number.isFinite(NaN) = false");
        passed++;
    } else {
        console.log("FAIL: Number.isFinite(NaN) should be false");
        failed++;
    }

    // Test 6: Zero is finite
    if (Number.isFinite(0)) {
        console.log("PASS: Number.isFinite(0) = true");
        passed++;
    } else {
        console.log("FAIL: Number.isFinite(0) should be true");
        failed++;
    }

    // ==================== Number.isNaN() ====================

    // Test 7: NaN is NaN
    if (Number.isNaN(NaN)) {
        console.log("PASS: Number.isNaN(NaN) = true");
        passed++;
    } else {
        console.log("FAIL: Number.isNaN(NaN) should be true");
        failed++;
    }

    // Test 8: Normal number is not NaN
    if (!Number.isNaN(42)) {
        console.log("PASS: Number.isNaN(42) = false");
        passed++;
    } else {
        console.log("FAIL: Number.isNaN(42) should be false");
        failed++;
    }

    // Test 9: Infinity is not NaN
    if (!Number.isNaN(Infinity)) {
        console.log("PASS: Number.isNaN(Infinity) = false");
        passed++;
    } else {
        console.log("FAIL: Number.isNaN(Infinity) should be false");
        failed++;
    }

    // Test 10: Zero is not NaN
    if (!Number.isNaN(0)) {
        console.log("PASS: Number.isNaN(0) = false");
        passed++;
    } else {
        console.log("FAIL: Number.isNaN(0) should be false");
        failed++;
    }

    // ==================== Number.isInteger() ====================

    // Test 11: Integer is an integer
    if (Number.isInteger(42)) {
        console.log("PASS: Number.isInteger(42) = true");
        passed++;
    } else {
        console.log("FAIL: Number.isInteger(42) should be true");
        failed++;
    }

    // Test 12: Negative integer is an integer
    if (Number.isInteger(-100)) {
        console.log("PASS: Number.isInteger(-100) = true");
        passed++;
    } else {
        console.log("FAIL: Number.isInteger(-100) should be true");
        failed++;
    }

    // Test 13: Zero is an integer
    if (Number.isInteger(0)) {
        console.log("PASS: Number.isInteger(0) = true");
        passed++;
    } else {
        console.log("FAIL: Number.isInteger(0) should be true");
        failed++;
    }

    // Test 14: Float is not an integer
    if (!Number.isInteger(3.14)) {
        console.log("PASS: Number.isInteger(3.14) = false");
        passed++;
    } else {
        console.log("FAIL: Number.isInteger(3.14) should be false");
        failed++;
    }

    // Test 15: Float 5.0 is an integer (has no fractional part)
    if (Number.isInteger(5.0)) {
        console.log("PASS: Number.isInteger(5.0) = true");
        passed++;
    } else {
        console.log("FAIL: Number.isInteger(5.0) should be true");
        failed++;
    }

    // Test 16: Infinity is not an integer
    if (!Number.isInteger(Infinity)) {
        console.log("PASS: Number.isInteger(Infinity) = false");
        passed++;
    } else {
        console.log("FAIL: Number.isInteger(Infinity) should be false");
        failed++;
    }

    // Test 17: NaN is not an integer
    if (!Number.isInteger(NaN)) {
        console.log("PASS: Number.isInteger(NaN) = false");
        passed++;
    } else {
        console.log("FAIL: Number.isInteger(NaN) should be false");
        failed++;
    }

    // ==================== Number.isSafeInteger() ====================

    // Test 18: Normal integer is safe
    if (Number.isSafeInteger(42)) {
        console.log("PASS: Number.isSafeInteger(42) = true");
        passed++;
    } else {
        console.log("FAIL: Number.isSafeInteger(42) should be true");
        failed++;
    }

    // Test 19: MAX_SAFE_INTEGER is safe
    if (Number.isSafeInteger(9007199254740991)) {
        console.log("PASS: Number.isSafeInteger(MAX_SAFE_INTEGER) = true");
        passed++;
    } else {
        console.log("FAIL: Number.isSafeInteger(MAX_SAFE_INTEGER) should be true");
        failed++;
    }

    // Test 20: MIN_SAFE_INTEGER is safe
    if (Number.isSafeInteger(-9007199254740991)) {
        console.log("PASS: Number.isSafeInteger(MIN_SAFE_INTEGER) = true");
        passed++;
    } else {
        console.log("FAIL: Number.isSafeInteger(MIN_SAFE_INTEGER) should be true");
        failed++;
    }

    // Test 21: MAX_SAFE_INTEGER + 1 is not safe
    if (!Number.isSafeInteger(9007199254740992)) {
        console.log("PASS: Number.isSafeInteger(MAX_SAFE_INTEGER + 1) = false");
        passed++;
    } else {
        console.log("FAIL: Number.isSafeInteger(MAX_SAFE_INTEGER + 1) should be false");
        failed++;
    }

    // Test 22: Float is not a safe integer
    if (!Number.isSafeInteger(3.14)) {
        console.log("PASS: Number.isSafeInteger(3.14) = false");
        passed++;
    } else {
        console.log("FAIL: Number.isSafeInteger(3.14) should be false");
        failed++;
    }

    // Test 23: Infinity is not a safe integer
    if (!Number.isSafeInteger(Infinity)) {
        console.log("PASS: Number.isSafeInteger(Infinity) = false");
        passed++;
    } else {
        console.log("FAIL: Number.isSafeInteger(Infinity) should be false");
        failed++;
    }

    // Test 24: NaN is not a safe integer
    if (!Number.isSafeInteger(NaN)) {
        console.log("PASS: Number.isSafeInteger(NaN) = false");
        passed++;
    } else {
        console.log("FAIL: Number.isSafeInteger(NaN) should be false");
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
