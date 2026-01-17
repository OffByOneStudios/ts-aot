// Test file descriptor based fs operations: fstatSync, fsyncSync, ftruncateSync, fdatasyncSync
import * as fs from 'fs';

const results = { passed: 0, failed: 0 };

function test(name: string, condition: boolean): void {
    if (condition) {
        console.log("PASS: " + name);
        results.passed = results.passed + 1;
    } else {
        console.log("FAIL: " + name);
        results.failed = results.failed + 1;
    }
}

function user_main(): number {
    console.log("=== Testing fs file descriptor operations ===\n");

    const testFile = "test_fd_ops.txt";
    const testContent = "Hello, World! Testing fd operations.";

    // Write test file
    fs.writeFileSync(testFile, testContent);

    // Verify file was written correctly (by length)
    const verifyContent = fs.readFileSync(testFile, "utf8");
    test("writeFileSync wrote content correctly", verifyContent.length === testContent.length);

    // Test 1: Open file and get fd
    const fd = fs.openSync(testFile, "r+");
    test("openSync returns valid fd", fd >= 0);

    // Test 2: fstatSync - get stats from fd
    const stats = fs.fstatSync(fd);
    test("fstatSync returns stats object", stats !== null && stats !== undefined);
    test("fstatSync stats.size matches content length", stats.size === testContent.length);
    test("fstatSync stats.isFile() returns true", stats.isFile() === true);
    test("fstatSync stats.isDirectory() returns false", stats.isDirectory() === false);

    // Test 3: fsyncSync - flush data to disk (should not throw)
    fs.fsyncSync(fd);
    test("fsyncSync completes without error", true);

    // Test 4: fdatasyncSync - flush data (not metadata) to disk
    fs.fdatasyncSync(fd);
    test("fdatasyncSync completes without error", true);

    // Close file for truncate test
    fs.closeSync(fd);

    // Test 5: ftruncateSync - truncate file via fd
    const fd2 = fs.openSync(testFile, "r+");
    const truncateLen = 10;
    fs.ftruncateSync(fd2, truncateLen);
    fs.closeSync(fd2);

    // Verify truncation (by length)
    const truncatedContent = fs.readFileSync(testFile, "utf8");
    test("ftruncateSync truncates file to correct length", truncatedContent.length === truncateLen);

    // Test 6: ftruncateSync with length 0
    const fd3 = fs.openSync(testFile, "r+");
    fs.ftruncateSync(fd3, 0);
    fs.closeSync(fd3);
    const emptyContent = fs.readFileSync(testFile, "utf8");
    test("ftruncateSync(fd, 0) empties file", emptyContent.length === 0);

    // Cleanup
    fs.unlinkSync(testFile);

    console.log("\n=== Summary ===");
    console.log("Passed: " + results.passed);
    console.log("Failed: " + results.failed);
    return results.failed > 0 ? 1 : 0;
}
