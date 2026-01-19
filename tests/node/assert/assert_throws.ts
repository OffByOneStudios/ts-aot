// Test assert.throws and assert.doesNotThrow with exception handling
import * as assert from 'assert';

function user_main(): number {
    console.log("=== Testing assert.throws / assert.doesNotThrow ===");

    // Test 1: assert.doesNotThrow with a non-throwing function
    console.log("Test 1: assert.doesNotThrow with non-throwing function");
    assert.doesNotThrow(() => {
        const result = 1 + 2;
    });
    console.log("PASS: assert.doesNotThrow completed without exception");

    // Test 2: assert.throws with a throwing function
    console.log("Test 2: assert.throws with throwing function");
    assert.throws(() => {
        throw new Error("Expected error");
    });
    console.log("PASS: assert.throws caught the exception");

    console.log("All tests passed!");
    return 0;
}
