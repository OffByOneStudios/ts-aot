// Test basic child_process.spawnSync functionality

import * as child_process from 'child_process';

function user_main(): number {
    console.log("=== Testing child_process.spawnSync ===");

    // Test 1: spawnSync with echo command
    console.log("Test 1: spawnSync echo command");
    const result = child_process.spawnSync('cmd.exe', ['/c', 'echo', 'Hello from spawnSync']);

    if (!result) {
        console.log("FAIL: result is null/undefined");
        return 1;
    }
    console.log("PASS: result is valid");

    // Test 2: Check status (exit code)
    console.log("Test 2: Check status");
    const status = result.status;
    console.log("Status:", status);
    if (status === 0) {
        console.log("PASS: Exit status is 0");
    } else {
        console.log("FAIL: Exit status should be 0");
        return 1;
    }

    // Test 3: Check stdout
    console.log("Test 3: Check stdout");
    const stdout = result.stdout;
    if (stdout) {
        const output = stdout.toString();
        console.log("stdout:", output);
        // Check if the output contains the expected text
        if (output.indexOf("Hello") >= 0) {
            console.log("PASS: stdout contains expected output");
        } else {
            console.log("FAIL: stdout doesn't contain expected output");
            return 1;
        }
    } else {
        console.log("FAIL: stdout is null");
        return 1;
    }

    // Test 4: Check stderr (should exist, may be empty)
    console.log("Test 4: Check stderr");
    if (result.stderr) {
        console.log("PASS: stderr exists");
    } else {
        console.log("FAIL: stderr is null");
        return 1;
    }

    // Test 5: Check signal (should be null for normal exit)
    console.log("Test 5: Check signal");
    const signal = result.signal;
    if (signal === undefined || signal === null) {
        console.log("PASS: signal is null for normal exit");
    } else {
        console.log("INFO: signal is", signal);
    }

    // Test 6: Check output array
    console.log("Test 6: Check output array");
    if (result.output) {
        console.log("output array length:", result.output.length);
        if (result.output.length >= 3) {
            console.log("PASS: output array has at least 3 elements");
        } else {
            console.log("FAIL: output array should have 3 elements");
            return 1;
        }
    } else {
        console.log("FAIL: output array is null");
        return 1;
    }

    console.log("\n=== All child_process.spawnSync tests passed! ===");
    return 0;
}
