// Test net socket state properties
import * as net from 'net';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    console.log("=== Net Socket State Properties Tests ===");

    // Create a socket
    const socket = new net.Socket();

    // Test 1: bytesRead initial value
    {
        const value = socket.bytesRead;
        if (value === 0) {
            console.log("PASS: bytesRead initial = 0");
            passed++;
        } else {
            console.log("FAIL: bytesRead expected 0, got: " + value);
            failed++;
        }
    }

    // Test 2: bytesWritten initial value
    {
        const value = socket.bytesWritten;
        if (value === 0) {
            console.log("PASS: bytesWritten initial = 0");
            passed++;
        } else {
            console.log("FAIL: bytesWritten expected 0, got: " + value);
            failed++;
        }
    }

    // Test 3: connecting initial value (boolean, should be falsy)
    {
        if (!socket.connecting) {
            console.log("PASS: connecting initial = false");
            passed++;
        } else {
            console.log("FAIL: connecting expected false");
            failed++;
        }
    }

    // Test 4: destroyed initial value (boolean, should be falsy)
    {
        if (!socket.destroyed) {
            console.log("PASS: destroyed initial = false");
            passed++;
        } else {
            console.log("FAIL: destroyed expected false");
            failed++;
        }
    }

    // Test 5: pending initial value (boolean, should be falsy)
    {
        if (!socket.pending) {
            console.log("PASS: pending initial = false");
            passed++;
        } else {
            console.log("FAIL: pending expected false");
            failed++;
        }
    }

    // Test 6: readyState initial value (should be 'closed' for unconnected socket)
    {
        const readyStateValue = socket.readyState;
        if (readyStateValue === "closed") {
            console.log("PASS: readyState initial = 'closed'");
            passed++;
        } else {
            console.log("FAIL: readyState expected 'closed', got: " + readyStateValue);
            failed++;
        }
    }

    console.log("");
    console.log("Results: " + passed + " passed, " + failed + " failed");

    return failed;
}
