// Test assert.rejects and assert.doesNotReject with async exception handling
import * as assert from 'assert';

async function user_main(): Promise<number> {
    console.log("=== Testing assert.rejects / assert.doesNotReject ===");

    // Test 1: assert.rejects with rejecting async function
    console.log("Test 1: assert.rejects with rejecting async function");
    await assert.rejects(async () => {
        throw new Error("Expected rejection");
    });
    console.log("PASS: assert.rejects detected rejection");

    // Test 2: assert.doesNotReject with resolving async function
    console.log("Test 2: assert.doesNotReject with resolving async function");
    await assert.doesNotReject(async () => {
        return "success";
    });
    console.log("PASS: assert.doesNotReject completed");

    // Test 3: assert.rejects with Promise.reject
    console.log("Test 3: assert.rejects with Promise.reject");
    await assert.rejects(Promise.reject(new Error("Rejected")));
    console.log("PASS: assert.rejects with direct promise");

    // Test 4: assert.doesNotReject with Promise.resolve
    console.log("Test 4: assert.doesNotReject with Promise.resolve");
    await assert.doesNotReject(Promise.resolve("ok"));
    console.log("PASS: assert.doesNotReject with direct promise");

    console.log("\nAll async assertion tests passed!");
    return 0;
}
