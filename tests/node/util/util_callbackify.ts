// Test util.callbackify()
// Converts Promise-returning functions to callback style

import * as util from 'util';

// Simple async function that returns a value
async function asyncDouble(value: number): Promise<number> {
    return value * 2;
}

// Async function that rejects
async function asyncError(msg: string): Promise<string> {
    throw new Error(msg);
}

function user_main(): number {
    console.log("Testing util.callbackify");

    // Test 1: Successful async function
    const callbackified = util.callbackify(asyncDouble);
    console.log("Test 1: Successful callback");

    callbackified(5, (err: any, result: number) => {
        if (err) {
            console.log("FAIL: unexpected error");
        } else if (result === 10) {
            console.log("PASS: Result is " + result);
        } else {
            console.log("FAIL: Expected 10, got " + result);
        }
    });

    // Test 2: Error handling (this test would require event loop processing)
    console.log("Test 2: Error callback");
    const errorCallbackified = util.callbackify(asyncError);

    errorCallbackified("test error", (err: any, result: string) => {
        if (err) {
            console.log("PASS: Error caught");
        } else {
            console.log("FAIL: Expected error, got result");
        }
    });

    console.log("util.callbackify test complete");
    return 0;
}
