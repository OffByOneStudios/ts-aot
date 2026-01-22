// Test CommonJS global objects: exports, module, module.exports

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: exports exists and is an object
    if (typeof exports === 'object' && exports !== null) {
        console.log("PASS: exports is an object");
        passed++;
    } else {
        console.log("FAIL: exports should be an object");
        failed++;
    }

    // Test 2: module exists and has exports property
    if (module && module.exports !== undefined) {
        console.log("PASS: module.exports exists");
        passed++;
    } else {
        console.log("FAIL: module.exports should exist");
        failed++;
    }

    // Test 3: exports and module.exports are the same object
    if (exports === module.exports) {
        console.log("PASS: exports === module.exports");
        passed++;
    } else {
        console.log("FAIL: exports and module.exports should be the same");
        failed++;
    }

    // Test 4: Can set property on exports
    exports.myValue = 42;
    if (exports.myValue === 42) {
        console.log("PASS: Can set property on exports");
        passed++;
    } else {
        console.log("FAIL: Should be able to set property on exports");
        failed++;
    }

    // Test 5: Setting on exports reflects in module.exports
    if (module.exports.myValue === 42) {
        console.log("PASS: exports change reflects in module.exports");
        passed++;
    } else {
        console.log("FAIL: exports change should reflect in module.exports");
        failed++;
    }

    console.log("\n=== Results: " + passed + " passed, " + failed + " failed ===");
    return failed > 0 ? 1 : 0;
}
