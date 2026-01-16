// Test for writable.setDefaultEncoding() method
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
    console.log("Testing writable.setDefaultEncoding()...");

    // Create a write stream
    const writable = fs.createWriteStream("test_output.txt");

    // Test setDefaultEncoding - this should not crash
    writable.setDefaultEncoding("utf8");
    test("setDefaultEncoding accepts encoding", true);

    // Clean up
    writable.end();

    // Delete the test file
    fs.unlinkSync("test_output.txt");

    console.log("");
    const passed = testsPassed;
    const failed = testsFailed;
    console.log("Results: " + passed + " passed, " + failed + " failed");

    return failed > 0 ? 1 : 0;
}
