// Test http.STATUS_CODES property access
import * as http from 'http';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    console.log("=== http.STATUS_CODES Tests ===");

    // ============================================================================
    // Test 1: STATUS_CODES object exists
    // ============================================================================
    {
        console.log("\nTest 1: STATUS_CODES object exists...");
        const codes = http.STATUS_CODES;
        console.log("typeof STATUS_CODES: " + typeof codes);

        if (codes !== null && codes !== undefined) {
            console.log("PASS: STATUS_CODES object exists");
            passed++;
        } else {
            console.log("FAIL: STATUS_CODES is null or undefined");
            failed++;
        }
    }

    // ============================================================================
    // Test 2: Access status code 200
    // ============================================================================
    {
        console.log("\nTest 2: Access STATUS_CODES[200]...");
        const msg200 = http.STATUS_CODES[200];
        console.log("STATUS_CODES[200]: " + msg200);

        if (msg200 === "OK") {
            console.log("PASS: STATUS_CODES[200] returns 'OK'");
            passed++;
        } else {
            console.log("FAIL: STATUS_CODES[200] should be 'OK', got: " + msg200);
            failed++;
        }
    }

    // ============================================================================
    // Test 3: Access status code 404
    // ============================================================================
    {
        console.log("\nTest 3: Access STATUS_CODES[404]...");
        const msg404 = http.STATUS_CODES[404];
        console.log("STATUS_CODES[404]: " + msg404);

        if (msg404 === "Not Found") {
            console.log("PASS: STATUS_CODES[404] returns 'Not Found'");
            passed++;
        } else {
            console.log("FAIL: STATUS_CODES[404] should be 'Not Found', got: " + msg404);
            failed++;
        }
    }

    // ============================================================================
    // Test 4: Access status code 500
    // ============================================================================
    {
        console.log("\nTest 4: Access STATUS_CODES[500]...");
        const msg500 = http.STATUS_CODES[500];
        console.log("STATUS_CODES[500]: " + msg500);

        if (msg500 === "Internal Server Error") {
            console.log("PASS: STATUS_CODES[500] returns 'Internal Server Error'");
            passed++;
        } else {
            console.log("FAIL: STATUS_CODES[500] should be 'Internal Server Error', got: " + msg500);
            failed++;
        }
    }

    // ============================================================================
    // Summary
    // ============================================================================
    console.log("");
    console.log("=== http.STATUS_CODES Results ===");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);
    console.log("Total: " + (passed + failed));

    return failed;
}
