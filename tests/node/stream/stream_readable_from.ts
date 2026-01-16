// Test stream.Readable.from() static method
import * as stream from 'stream';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    console.log("=== Stream Readable.from() Tests ===");

    // Test 1: Create a readable stream from an array
    {
        const data = ["hello", "world", "test"];
        const readable = stream.Readable.from(data);
        if (readable) {
            console.log("PASS: Created readable from array");
            passed++;
        } else {
            console.log("FAIL: Could not create readable from array");
            failed++;
        }
    }

    // Test 2: Check the readable has Readable properties
    {
        const data = [1, 2, 3];
        const readable = stream.Readable.from(data);
        // Check it has readable property
        const hasReadable = typeof readable.readable !== 'undefined';
        if (hasReadable) {
            console.log("PASS: Readable.from() result has readable property");
            passed++;
        } else {
            console.log("FAIL: Readable.from() result missing readable property");
            failed++;
        }
    }

    // Test 3: Create from empty array
    {
        const data: string[] = [];
        const readable = stream.Readable.from(data);
        if (readable) {
            console.log("PASS: Created readable from empty array");
            passed++;
        } else {
            console.log("FAIL: Could not create readable from empty array");
            failed++;
        }
    }

    console.log("");
    console.log("Results: " + passed + " passed, " + failed + " failed");

    return failed;
}
