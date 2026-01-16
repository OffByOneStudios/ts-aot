// Test for HTTP module constants
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
    console.log("Testing HTTP module constants...");

    const http = require('http');

    // Test http.maxHeaderSize
    const maxHeaderSize = http.maxHeaderSize;
    test("http.maxHeaderSize is 16384", maxHeaderSize === 16384);

    console.log("");
    const passed = testsPassed;
    const failed = testsFailed;
    console.log("Results: " + passed + " passed, " + failed + " failed");

    return failed > 0 ? 1 : 0;
}
