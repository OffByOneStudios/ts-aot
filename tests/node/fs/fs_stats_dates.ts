// Test for fs.Stats time properties returning Date objects
import * as fs from 'fs';

async function user_main(): Promise<number> {
    console.log("=== fs.Stats Date Properties Test ===");

    // Create a test file
    const testFile = "tests/test_stats_dates.txt";
    fs.writeFileSync(testFile, "Hello, World!");

    let testsPassed = 0;
    let testsFailed = 0;

    // Test 1: statSync returns stats object
    console.log("\n1. statSync returns stats:");
    const stats = fs.statSync(testFile);
    if (stats) {
        console.log("  PASS: stats object returned");
        testsPassed++;
    } else {
        console.log("  FAIL: stats object is null");
        testsFailed++;
    }

    // Test 2: mtimeMs is a number
    console.log("\n2. mtimeMs is a number:");
    const mtimeMs = stats.mtimeMs;
    if (typeof mtimeMs === 'number' && mtimeMs > 0) {
        console.log("  PASS: mtimeMs = " + mtimeMs);
        testsPassed++;
    } else {
        console.log("  FAIL: mtimeMs should be a positive number");
        testsFailed++;
    }

    // Test 3: mtime.getTime() works
    console.log("\n3. mtime.getTime() works:");
    const mtime = stats.mtime;
    if (mtime) {
        const timeVal = mtime.getTime();
        console.log("  PASS: mtime.getTime() = " + timeVal);
        testsPassed++;
    } else {
        console.log("  FAIL: mtime is null");
        testsFailed++;
    }

    // Test 4: atime.getTime() works
    console.log("\n4. atime.getTime() works:");
    const atime = stats.atime;
    if (atime) {
        const timeVal = atime.getTime();
        console.log("  PASS: atime.getTime() = " + timeVal);
        testsPassed++;
    } else {
        console.log("  FAIL: atime is null");
        testsFailed++;
    }

    // Test 5: ctime.getTime() works
    console.log("\n5. ctime.getTime() works:");
    const ctime = stats.ctime;
    if (ctime) {
        const timeVal = ctime.getTime();
        console.log("  PASS: ctime.getTime() = " + timeVal);
        testsPassed++;
    } else {
        console.log("  FAIL: ctime is null");
        testsFailed++;
    }

    // Test 6: birthtime.getTime() works
    console.log("\n6. birthtime.getTime() works:");
    const birthtime = stats.birthtime;
    if (birthtime) {
        const timeVal = birthtime.getTime();
        console.log("  PASS: birthtime.getTime() = " + timeVal);
        testsPassed++;
    } else {
        console.log("  FAIL: birthtime is null");
        testsFailed++;
    }

    // Test 7: mtime.toISOString() works
    console.log("\n7. mtime.toISOString() works:");
    if (mtime) {
        const isoStr = mtime.toISOString();
        if (isoStr && isoStr.length > 0) {
            console.log("  PASS: mtime.toISOString() = " + isoStr);
            testsPassed++;
        } else {
            console.log("  FAIL: toISOString returned empty string");
            testsFailed++;
        }
    } else {
        console.log("  FAIL: mtime is null");
        testsFailed++;
    }

    // Test 8: Date times are consistent with Ms values
    console.log("\n8. mtime.getTime() matches mtimeMs:");
    if (mtime) {
        const dateMs = mtime.getTime();
        const diff = Math.abs(dateMs - mtimeMs);
        if (diff < 1) {
            console.log("  PASS: mtime.getTime() matches mtimeMs exactly");
            testsPassed++;
        } else {
            console.log("  FAIL: mtime.getTime() = " + dateMs + ", mtimeMs = " + mtimeMs + ", diff = " + diff);
            testsFailed++;
        }
    } else {
        console.log("  FAIL: mtime is null");
        testsFailed++;
    }

    // Clean up
    fs.unlinkSync(testFile);

    console.log("\n=== Results ===");
    console.log("Passed: " + testsPassed);
    console.log("Failed: " + testsFailed);

    return testsFailed === 0 ? 0 : 1;
}
