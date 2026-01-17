// Test extended assert functions: fail, ifError, match, doesNotMatch
import * as assert from 'assert';

let testsPassed = 0;

function test(name: string, fn: () => void): void {
    // Since we don't have try/catch, if the function throws (assert fails),
    // the program exits. So if we get here, the test passed.
    fn();
    console.log("PASS: " + name);
    testsPassed++;
}

function user_main(): number {
    console.log("=== Testing extended assert functions ===");

    // Test 1: assert.match with matching string
    test("assert.match with matching string", () => {
        // This should NOT throw (string matches regex)
        assert.match("hello world", /world/);
    });

    // Test 2: assert.match with full string match
    test("assert.match with full match", () => {
        assert.match("test123", /test\d+/);
    });

    // Test 3: assert.doesNotMatch with non-matching string
    test("assert.doesNotMatch with non-matching string", () => {
        // This should NOT throw (string does not match)
        assert.doesNotMatch("hello world", /xyz/);
    });

    // Test 4: assert.doesNotMatch with different pattern
    test("assert.doesNotMatch with different pattern", () => {
        assert.doesNotMatch("abc", /\d+/);
    });

    // Test 5: assert.ifError with null/undefined
    test("assert.ifError with null", () => {
        // null is falsy, so this should NOT throw
        assert.ifError(null);
    });

    // Test 6: assert.ifError with undefined
    test("assert.ifError with undefined", () => {
        assert.ifError(undefined);
    });

    // Test 7: assert.doesNotThrow with non-throwing function
    test("assert.doesNotThrow with safe function", () => {
        assert.doesNotThrow(() => {
            const x = 1 + 1;
        });
    });

    // Note: We can't test assert.fail() because it always exits
    // Note: We can't test assert.throws() because we don't have try/catch
    // Note: assert.rejects/doesNotReject are stubs with warnings

    console.log("");
    console.log("=== Test Summary ===");
    console.log("Passed: " + testsPassed);
    console.log("All tests passed!");

    return 0;
}
