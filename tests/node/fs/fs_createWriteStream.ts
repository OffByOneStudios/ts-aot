// Test for fs.createWriteStream with options
import * as fs from 'fs';

async function user_main(): Promise<number> {
    console.log("=== fs.createWriteStream() Test ===");

    let testsPassed = 0;
    let testsFailed = 0;

    // Test 1: Basic createWriteStream
    console.log("\n1. Basic createWriteStream:");
    const testFile1 = "tests/test_writestream1.txt";
    const stream1 = fs.createWriteStream(testFile1);
    if (stream1) {
        console.log("  PASS: Stream created");
        testsPassed++;

        // Check pending property
        if (stream1.pending === true) {
            console.log("  PASS: pending is true before write");
            testsPassed++;
        } else {
            console.log("  FAIL: pending should be true before write");
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

        // Check bytesWritten starts at 0
        if (stream1.bytesWritten === 0) {
            console.log("  PASS: bytesWritten is 0 initially");
            testsPassed++;
        } else {
            console.log("  FAIL: bytesWritten should be 0, got " + stream1.bytesWritten);
            testsFailed++;
        }

        // Clean up
        stream1.end();
    } else {
        console.log("  FAIL: Stream not created");
        testsFailed++;
    }

    // Test 2: createWriteStream with flags option (append)
    console.log("\n2. createWriteStream with flags option:");
    const testFile2 = "tests/test_writestream2.txt";
    // First create the file
    fs.writeFileSync(testFile2, "Initial content\n");
    const stream2 = fs.createWriteStream(testFile2, { flags: "a" });
    if (stream2) {
        console.log("  PASS: Stream created with append flag");
        testsPassed++;
        stream2.end();
    } else {
        console.log("  FAIL: Stream not created with flags option");
        testsFailed++;
    }

    // Test 3: createWriteStream with autoClose option
    console.log("\n3. createWriteStream with autoClose option:");
    const testFile3 = "tests/test_writestream3.txt";
    const stream3 = fs.createWriteStream(testFile3, { autoClose: false });
    if (stream3) {
        console.log("  PASS: Stream created with autoClose=false");
        testsPassed++;
        stream3.end();
    } else {
        console.log("  FAIL: Stream not created with autoClose option");
        testsFailed++;
    }

    // Test 4: createWriteStream with start option
    console.log("\n4. createWriteStream with start option:");
    const testFile4 = "tests/test_writestream4.txt";
    // Create file with initial content
    fs.writeFileSync(testFile4, "XXXXXXXXXX");
    const stream4 = fs.createWriteStream(testFile4, { start: 2 });
    if (stream4) {
        console.log("  PASS: Stream created with start option");
        testsPassed++;
        stream4.end();
    } else {
        console.log("  FAIL: Stream not created with start option");
        testsFailed++;
    }

    // Clean up test files
    try { fs.unlinkSync(testFile1); } catch (e) {}
    try { fs.unlinkSync(testFile2); } catch (e) {}
    try { fs.unlinkSync(testFile3); } catch (e) {}
    try { fs.unlinkSync(testFile4); } catch (e) {}

    console.log("\n=== Results ===");
    console.log("Passed: " + testsPassed);
    console.log("Failed: " + testsFailed);

    return testsFailed === 0 ? 0 : 1;
}
