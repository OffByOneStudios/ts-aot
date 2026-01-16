// Test util.promisify()
import * as util from 'util';

function test(name: string, condition: boolean): void {
    if (condition) {
        console.log("PASS: " + name);
    } else {
        console.log("FAIL: " + name);
    }
}

// Callback-style function that succeeds
function successCallback(value: number, callback: (err: any, result: number) => void): void {
    callback(null, value * 2);
}

// Callback-style function that fails
function errorCallback(value: number, callback: (err: any, result?: number) => void): void {
    callback(new Error("Test error"));
}

// Callback-style function with no result
function noResultCallback(callback: (err: any) => void): void {
    callback(null);
}

async function user_main(): Promise<number> {
    // Test 1: promisified function returns a promise that resolves with correct value
    const promisifiedSuccess = util.promisify(successCallback);
    try {
        const result = await promisifiedSuccess(5);
        test("promisified function resolves with correct value", result === 10);
    } catch (e) {
        test("promisified function resolves with correct value", false);
    }

    // Test 2: promisified function returns a promise that rejects on error
    const promisifiedError = util.promisify(errorCallback);
    try {
        await promisifiedError(5);
        test("promisified function rejects on error", false);
    } catch (e: any) {
        test("promisified function rejects on error", e !== null && e !== undefined);
    }

    // Test 3: promisified function with no result resolves with undefined/null
    const promisifiedNoResult = util.promisify(noResultCallback);
    try {
        const result = await promisifiedNoResult();
        test("promisified function with no result resolves", result === undefined || result === null);
    } catch (e) {
        test("promisified function with no result resolves", false);
    }

    // Test 4: multiple sequential calls work
    try {
        const result1 = await promisifiedSuccess(3);
        const result2 = await promisifiedSuccess(7);
        test("multiple sequential calls work", result1 === 6 && result2 === 14);
    } catch (e) {
        test("multiple sequential calls work", false);
    }

    return 0;
}
