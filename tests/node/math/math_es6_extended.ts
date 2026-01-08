// Test extended ES6 Math methods

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test Math.cbrt
    if (Math.cbrt(27) === 3) {
        console.log("PASS: Math.cbrt(27) = 3");
        passed++;
    } else {
        console.log("FAIL: Math.cbrt(27)");
        failed++;
    }

    // Test Math.log10
    if (Math.log10(100) === 2) {
        console.log("PASS: Math.log10(100) = 2");
        passed++;
    } else {
        console.log("FAIL: Math.log10(100)");
        failed++;
    }

    // Test Math.log2
    if (Math.log2(8) === 3) {
        console.log("PASS: Math.log2(8) = 3");
        passed++;
    } else {
        console.log("FAIL: Math.log2(8)");
        failed++;
    }

    // Test Math.hypot
    if (Math.hypot(3, 4) === 5) {
        console.log("PASS: Math.hypot(3, 4) = 5");
        passed++;
    } else {
        console.log("FAIL: Math.hypot(3, 4)");
        failed++;
    }

    // Test Math.expm1
    if (Math.expm1(0) === 0) {
        console.log("PASS: Math.expm1(0) = 0");
        passed++;
    } else {
        console.log("FAIL: Math.expm1(0)");
        failed++;
    }

    // Test Math.log1p
    if (Math.log1p(0) === 0) {
        console.log("PASS: Math.log1p(0) = 0");
        passed++;
    } else {
        console.log("FAIL: Math.log1p(0)");
        failed++;
    }

    // Test Math.fround
    if (Math.fround(1.5) === 1.5) {
        console.log("PASS: Math.fround(1.5) = 1.5");
        passed++;
    } else {
        console.log("FAIL: Math.fround(1.5)");
        failed++;
    }

    // Test Math.clz32
    if (Math.clz32(1) === 31) {
        console.log("PASS: Math.clz32(1) = 31");
        passed++;
    } else {
        console.log("FAIL: Math.clz32(1) - got " + Math.clz32(1));
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
