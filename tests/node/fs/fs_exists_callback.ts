// Test fs.exists() callback version (deprecated but still used)
import * as fs from 'fs';

function user_main(): number {
    console.log("=== fs.exists() Callback Tests ===");
    let passed = 0;
    let failed = 0;
    let completed = 0;
    const total = 2;

    // Test 1: Check if a file that exists
    console.log("\n1. fs.exists() on existing file:");
    const testFile = "tests/node/fs/fs_exists_callback.ts";
    fs.exists(testFile, (exists: boolean) => {
        if (exists) {
            console.log("  File exists: true");
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL - file should exist");
            failed++;
        }
        completed++;
        checkDone();
    });

    // Test 2: Check if a file that doesn't exist
    console.log("\n2. fs.exists() on non-existing file:");
    fs.exists("/nonexistent/path/to/file.txt", (exists: boolean) => {
        if (!exists) {
            console.log("  File exists: false");
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL - file should not exist");
            failed++;
        }
        completed++;
        checkDone();
    });

    function checkDone(): void {
        if (completed === total) {
            console.log("\n========================================");
            console.log("Results: " + passed + "/" + total + " tests passed");
            if (failed === 0) {
                console.log("All fs.exists() tests passed!");
            }
        }
    }

    return 0;
}
