// Test child_process.exec with callback

import * as child_process from 'child_process';
import { Buffer } from 'buffer';

function user_main(): number {
    console.log("=== Testing child_process.exec with callback ===");

    // Test 1: exec with callback
    console.log("Test 1: exec with callback");

    child_process.exec('cmd.exe /c echo Hello from exec', (error: any, stdout: Buffer, stderr: Buffer) => {
        console.log("Callback invoked!");

        if (error) {
            console.log("Error:", error.message);
        } else {
            console.log("PASS: No error");
        }

        // Check if stdout is a buffer and has data
        if (stdout) {
            console.log("PASS: stdout is present");
            // Use Buffer.from to get string content
            const str = stdout.toString('utf8');
            console.log("stdout content:", str);
            if (str && str.indexOf("Hello") >= 0) {
                console.log("PASS: stdout contains expected text");
            } else {
                console.log("FAIL: stdout doesn't contain expected text");
            }
        } else {
            console.log("FAIL: stdout is null/undefined");
        }

        console.log("\n=== exec callback test completed! ===");
    });

    console.log("exec() returned, waiting for callback...");

    return 0;
}
