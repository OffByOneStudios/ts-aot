// Test child_process.execFileSync functionality

import * as child_process from 'child_process';

function user_main(): number {
    console.log("=== Testing child_process.execFileSync ===");

    // Test 1: execFileSync with cmd.exe
    console.log("Test 1: execFileSync echo command");
    const result = child_process.execFileSync('cmd.exe', ['/c', 'echo', 'Hello from execFileSync']);

    if (!result) {
        console.log("FAIL: result is null/undefined");
        return 1;
    }
    console.log("PASS: result is valid");

    // Test 2: Check result is Buffer
    console.log("Test 2: Check result is Buffer");
    const output = result.toString();
    console.log("output:", output);
    if (output.indexOf("Hello") >= 0) {
        console.log("PASS: output contains expected text");
    } else {
        console.log("FAIL: output doesn't contain expected text");
        return 1;
    }

    console.log("\n=== All child_process.execFileSync tests passed! ===");
    return 0;
}
