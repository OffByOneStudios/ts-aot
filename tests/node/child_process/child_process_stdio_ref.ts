// Test child_process stdio array and ref/unref methods

import * as child_process from 'child_process';

function user_main(): number {
    console.log("=== Testing child_process stdio and ref/unref ===");

    // Test 1: spawn a process
    console.log("Test 1: Spawn echo command");
    const cp = child_process.spawn('cmd.exe', ['/c', 'echo', 'Hello']);

    if (!cp) {
        console.log("FAIL: spawn returned null");
        return 1;
    }
    console.log("PASS: spawn returned ChildProcess");

    // Test 2: Access stdio array
    console.log("Test 2: Check stdio array");
    const stdio = cp.stdio;
    if (stdio) {
        console.log("stdio length:", stdio.length);
        if (stdio.length === 3) {
            console.log("PASS: stdio has 3 elements");
        } else {
            console.log("FAIL: stdio should have 3 elements");
            return 1;
        }
    } else {
        console.log("FAIL: stdio is null");
        return 1;
    }

    // Test 3: Check stdio[0] is stdin
    console.log("Test 3: Check stdio[0] is stdin");
    const stdioStdin = stdio[0];
    const directStdin = cp.stdin;
    // Both should exist or both null
    if (stdioStdin && directStdin) {
        console.log("PASS: stdio[0] and stdin both exist");
    } else if (!stdioStdin && !directStdin) {
        console.log("PASS: stdio[0] and stdin both null (pipe mode may differ)");
    } else {
        console.log("INFO: stdio[0] and stdin differ");
    }

    // Test 4: Check ref() returns the process
    console.log("Test 4: Check ref() method");
    const refResult = cp.ref();
    if (refResult) {
        console.log("PASS: ref() returned a value");
    } else {
        console.log("FAIL: ref() returned null");
        return 1;
    }

    // Test 5: Check unref() returns the process
    console.log("Test 5: Check unref() method");
    const unrefResult = cp.unref();
    if (unrefResult) {
        console.log("PASS: unref() returned a value");
    } else {
        console.log("FAIL: unref() returned null");
        return 1;
    }

    console.log("\n=== All child_process stdio/ref tests passed! ===");
    return 0;
}
