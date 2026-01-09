// Test Math.imul()

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Basic multiplication
    if (Math.imul(2, 4) === 8) {
        console.log("PASS: Math.imul(2, 4) = 8");
        passed++;
    } else {
        console.log("FAIL: Math.imul(2, 4) - got " + Math.imul(2, 4));
        failed++;
    }

    // Test 2: Multiplication with negative
    if (Math.imul(-1, 8) === -8) {
        console.log("PASS: Math.imul(-1, 8) = -8");
        passed++;
    } else {
        console.log("FAIL: Math.imul(-1, 8) - got " + Math.imul(-1, 8));
        failed++;
    }

    // Test 3: Both negative
    if (Math.imul(-2, -2) === 4) {
        console.log("PASS: Math.imul(-2, -2) = 4");
        passed++;
    } else {
        console.log("FAIL: Math.imul(-2, -2) - got " + Math.imul(-2, -2));
        failed++;
    }

    // Test 4: Zero
    if (Math.imul(0, 100) === 0) {
        console.log("PASS: Math.imul(0, 100) = 0");
        passed++;
    } else {
        console.log("FAIL: Math.imul(0, 100) - got " + Math.imul(0, 100));
        failed++;
    }

    // Test 5: Large numbers that would overflow in regular 64-bit
    // Math.imul wraps at 32 bits - 0xffffffff is -1 as i32
    if (Math.imul(0xffffffff, 5) === -5) {
        console.log("PASS: Math.imul(0xffffffff, 5) = -5");
        passed++;
    } else {
        console.log("FAIL: Math.imul(0xffffffff, 5) - got " + Math.imul(0xffffffff, 5));
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
