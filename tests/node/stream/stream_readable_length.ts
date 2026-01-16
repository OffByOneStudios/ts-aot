// Test readable.readableLength property
import * as stream from 'stream';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    console.log("=== Stream Readable Length Tests ===");

    // Test 1: Check readableLength on Readable.from() result
    {
        const data = ["hello", "world"];
        const readable = stream.Readable.from(data);
        const length = readable.readableLength;
        // Should be 0 for simple streams (no internal buffer)
        if (length === 0) {
            console.log("PASS: readableLength is 0 (no internal buffer)");
            passed++;
        } else {
            console.log("FAIL: readableLength = " + length + ", expected 0");
            failed++;
        }
    }

    // Test 2: Check readableLength is a number
    {
        const data = [1, 2, 3];
        const readable = stream.Readable.from(data);
        const length = readable.readableLength;
        if (typeof length === 'number') {
            console.log("PASS: readableLength is a number");
            passed++;
        } else {
            console.log("FAIL: readableLength is not a number");
            failed++;
        }
    }

    console.log("");
    console.log("Results: " + passed + " passed, " + failed + " failed");

    return failed;
}
