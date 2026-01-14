// Test HTTP module constants and utilities
import * as http from 'http';

function user_main(): number {
    console.log("=== HTTP Constants and Utilities Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: http.METHODS
    console.log("\n1. http.METHODS:");
    const methods = http.METHODS;
    console.log("  METHODS is array: " + Array.isArray(methods));
    console.log("  METHODS length: " + methods.length);
    if (Array.isArray(methods) && methods.length > 0) {
        console.log("  First method: " + methods[0]);
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 2: http.STATUS_CODES
    console.log("\n2. http.STATUS_CODES:");
    const statusCodes = http.STATUS_CODES;
    if (statusCodes) {
        const ok = statusCodes["200"];
        const notFound = statusCodes["404"];
        console.log("  STATUS_CODES[200] = " + ok);
        console.log("  STATUS_CODES[404] = " + notFound);
        if (ok && notFound) {
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (missing values)");
            failed++;
        }
    } else {
        console.log("  FAIL (STATUS_CODES is null)");
        failed++;
    }

    // Test 3: http.maxHeaderSize
    console.log("\n3. http.maxHeaderSize:");
    const maxSize = http.maxHeaderSize;
    console.log("  maxHeaderSize = " + maxSize);
    if (maxSize > 0) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected positive number)");
        failed++;
    }

    // Test 4: http.validateHeaderName (valid name)
    console.log("\n4. http.validateHeaderName (valid):");
    let validPassed = false;
    http.validateHeaderName("Content-Type");
    validPassed = true;  // If we get here, it didn't throw
    console.log("  validateHeaderName('Content-Type') - no throw");
    if (validPassed) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 5: http.globalAgent
    console.log("\n5. http.globalAgent:");
    const agent = http.globalAgent;
    console.log("  globalAgent exists: " + (agent !== null && agent !== undefined));
    if (agent) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");
    if (failed === 0) {
        console.log("All HTTP constants tests passed!");
    }

    return failed;
}
