// Test for fs.createReadStream with options
import * as fs from 'fs';

async function user_main(): Promise<number> {
    console.log("=== fs.createReadStream() Test ===");

    // Create a test file with known content
    const testFile = "tests/test_readstream.txt";
    const content = "Hello, World! This is a test file for createReadStream.\n";
    fs.writeFileSync(testFile, content);

    let testsPassed = 0;
    let testsFailed = 0;

    // Test 1: Basic createReadStream
    console.log("\n1. Basic createReadStream:");
    const stream1 = fs.createReadStream(testFile);
    if (stream1) {
        console.log("  PASS: Stream created");
        testsPassed++;

        // Check pending property
        if (stream1.pending === true) {
            console.log("  PASS: pending is true before start");
            testsPassed++;
        } else {
            console.log("  FAIL: pending should be true before start");
            testsFailed++;
        }

        // Check path property
        const path = stream1.path;
        if (path) {
            console.log("  PASS: path is '" + path + "'");
            testsPassed++;
        } else {
            console.log("  FAIL: path should be set");
            testsFailed++;
        }

        // Check bytesRead starts at 0
        if (stream1.bytesRead === 0) {
            console.log("  PASS: bytesRead is 0 initially");
            testsPassed++;
        } else {
            console.log("  FAIL: bytesRead should be 0, got " + stream1.bytesRead);
            testsFailed++;
        }
    } else {
        console.log("  FAIL: Stream not created");
        testsFailed++;
    }

    // Test 2: createReadStream with options - start position
    console.log("\n2. createReadStream with start option:");
    const stream2 = fs.createReadStream(testFile, { start: 7 });
    if (stream2) {
        console.log("  PASS: Stream created with start option");
        testsPassed++;
    } else {
        console.log("  FAIL: Stream not created with options");
        testsFailed++;
    }

    // Test 3: createReadStream with highWaterMark option
    console.log("\n3. createReadStream with highWaterMark option:");
    const stream3 = fs.createReadStream(testFile, { highWaterMark: 1024 });
    if (stream3) {
        console.log("  PASS: Stream created with highWaterMark option");
        testsPassed++;
    } else {
        console.log("  FAIL: Stream not created with highWaterMark option");
        testsFailed++;
    }

    // Test 4: createReadStream with autoClose option
    console.log("\n4. createReadStream with autoClose option:");
    const stream4 = fs.createReadStream(testFile, { autoClose: false });
    if (stream4) {
        console.log("  PASS: Stream created with autoClose=false");
        testsPassed++;
    } else {
        console.log("  FAIL: Stream not created with autoClose option");
        testsFailed++;
    }

    // Clean up
    fs.unlinkSync(testFile);

    console.log("\n=== Results ===");
    console.log("Passed: " + testsPassed);
    console.log("Failed: " + testsFailed);

    return testsFailed === 0 ? 0 : 1;
}
