// Test basic child_process.spawn functionality

import * as child_process from 'child_process';

function user_main(): number {
    console.log("=== Testing child_process.spawn ===");

    // Test 1: Spawn with command and args
    console.log("Test 1: Spawn echo command");
    const proc = child_process.spawn('cmd.exe', ['/c', 'echo', 'Hello from child process']);

    console.log("Spawn returned");

    if (!proc) {
        console.log("FAIL: proc is null/undefined");
        return 1;
    }
    console.log("PASS: proc is valid");

    // Test 2: Check PID
    console.log("Test 2: Check PID");
    const pid = proc.pid;
    console.log("PID:", pid);
    if (pid > 0) {
        console.log("PASS: Valid PID");
    } else {
        console.log("FAIL: Invalid PID");
        return 1;
    }

    // Test 3: Check connected status
    console.log("Test 3: Check connected");
    if (proc.connected) {
        console.log("PASS: Process is connected");
    } else {
        console.log("FAIL: Process should be connected");
        return 1;
    }

    // Test 4: Check killed status
    console.log("Test 4: Check killed");
    if (!proc.killed) {
        console.log("PASS: Process is not killed");
    } else {
        console.log("FAIL: Process should not be killed");
        return 1;
    }

    // Test 5: Check spawnfile
    console.log("Test 5: Check spawnfile");
    const spawnfile = proc.spawnfile;
    console.log("spawnfile:", spawnfile);
    if (spawnfile === 'cmd.exe') {
        console.log("PASS: Correct spawnfile");
    } else {
        console.log("FAIL: spawnfile should be cmd.exe");
        return 1;
    }

    // Test 6: Check stdin exists
    console.log("Test 6: Check stdin");
    if (proc.stdin) {
        console.log("PASS: stdin exists");
    } else {
        console.log("FAIL: stdin should exist");
        return 1;
    }

    // Test 7: Check stdout exists
    console.log("Test 7: Check stdout");
    if (proc.stdout) {
        console.log("PASS: stdout exists");
    } else {
        console.log("FAIL: stdout should exist");
        return 1;
    }

    // Test 8: Check stderr exists
    console.log("Test 8: Check stderr");
    if (proc.stderr) {
        console.log("PASS: stderr exists");
    } else {
        console.log("FAIL: stderr should exist");
        return 1;
    }

    // Test 9: Listen to stdout data
    console.log("Test 9: stdout.on('data')");
    proc.stdout.on('data', (data: Buffer) => {
        console.log("stdout:", data.toString().trim());
    });

    // Test 10: Listen to exit event
    console.log("Test 10: Listen to exit");
    proc.on('exit', (code: number, signal: string) => {
        console.log("Exit code:", code);
    });

    console.log("\n=== All child_process.spawn tests passed! ===");
    return 0;
}
