// Test for readable.unshift() method
import * as fs from 'fs';

let testsPassed = 0;
let testsFailed = 0;

function test(name: string, condition: boolean): void {
    if (condition) {
        console.log("PASS: " + name);
        testsPassed++;
    } else {
        console.log("FAIL: " + name);
        testsFailed++;
    }
}

function user_main(): number {
    console.log("Testing readable.unshift()...");

    // Create a read stream to test unshift
    fs.writeFileSync("test_read.txt", "hello world");
    const readable = fs.createReadStream("test_read.txt");

    // Test unshift with a value - this should not crash
    readable.unshift("test");
    test("unshift accepts a chunk", true);

    // readableDidRead should be true after unshift
    test("readableDidRead is true after unshift", readable.readableDidRead === true);

    // Clean up
    fs.unlinkSync("test_read.txt");

    console.log("");
    const passed = testsPassed;
    const failed = testsFailed;
    console.log("Results: " + passed + " passed, " + failed + " failed");

    return failed > 0 ? 1 : 0;
}
