// Test ES6 Math methods

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Math.sign positive
    if (Math.sign(5) === 1) {
        console.log("PASS: Math.sign(5) = 1");
        passed++;
    } else {
        console.log("FAIL: Math.sign(5) - expected 1, got " + Math.sign(5));
        failed++;
    }

    // Test 2: Math.sign negative
    if (Math.sign(-5) === -1) {
        console.log("PASS: Math.sign(-5) = -1");
        passed++;
    } else {
        console.log("FAIL: Math.sign(-5) - expected -1, got " + Math.sign(-5));
        failed++;
    }

    // Test 3: Math.sign zero
    if (Math.sign(0) === 0) {
        console.log("PASS: Math.sign(0) = 0");
        passed++;
    } else {
        console.log("FAIL: Math.sign(0) - expected 0, got " + Math.sign(0));
        failed++;
    }

    // Test 4: Math.trunc positive
    if (Math.trunc(4.7) === 4) {
        console.log("PASS: Math.trunc(4.7) = 4");
        passed++;
    } else {
        console.log("FAIL: Math.trunc(4.7) - expected 4, got " + Math.trunc(4.7));
        failed++;
    }

    // Test 5: Math.trunc negative
    if (Math.trunc(-4.7) === -4) {
        console.log("PASS: Math.trunc(-4.7) = -4");
        passed++;
    } else {
        console.log("FAIL: Math.trunc(-4.7) - expected -4, got " + Math.trunc(-4.7));
        failed++;
    }

    // Test 6: Math.trunc already integer
    if (Math.trunc(5) === 5) {
        console.log("PASS: Math.trunc(5) = 5");
        passed++;
    } else {
        console.log("FAIL: Math.trunc(5) - expected 5, got " + Math.trunc(5));
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
