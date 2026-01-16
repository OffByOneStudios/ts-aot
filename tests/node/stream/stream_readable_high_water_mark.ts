// Test readable.readableHighWaterMark property
import * as stream from 'stream';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    console.log("=== Stream Readable High Water Mark Tests ===");

    // Test 1: Check default high water mark on Readable.from() result
    {
        const data = ["hello", "world"];
        const readable = stream.Readable.from(data);
        const hwm = readable.readableHighWaterMark;
        // Default is 16KB = 16384
        if (hwm === 16384) {
            console.log("PASS: Default readableHighWaterMark is 16384");
            passed++;
        } else {
            console.log("FAIL: readableHighWaterMark = " + hwm + ", expected 16384");
            failed++;
        }
    }

    // Test 2: Check readableHighWaterMark is a number
    {
        const data = [1, 2, 3];
        const readable = stream.Readable.from(data);
        const hwm = readable.readableHighWaterMark;
        if (typeof hwm === 'number') {
            console.log("PASS: readableHighWaterMark is a number");
            passed++;
        } else {
            console.log("FAIL: readableHighWaterMark is not a number");
            failed++;
        }
    }

    // Test 3: Multiple reads of the same property
    {
        const data = ["test"];
        const readable = stream.Readable.from(data);
        const hwm1 = readable.readableHighWaterMark;
        const hwm2 = readable.readableHighWaterMark;
        if (hwm1 === hwm2) {
            console.log("PASS: readableHighWaterMark is consistent");
            passed++;
        } else {
            console.log("FAIL: readableHighWaterMark is inconsistent");
            failed++;
        }
    }

    console.log("");
    console.log("Results: " + passed + " passed, " + failed + " failed");

    return failed;
}
