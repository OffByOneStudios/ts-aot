// Test util.types.isPromise()
import * as util from 'util';

function user_main(): number {
    console.log("=== util.types.isPromise() Tests ===");
    console.log("");
    let failures = 0;

    // Test 1: Promise.resolve() returns a Promise
    {
        const p = Promise.resolve(42);
        if (util.types.isPromise(p)) {
            console.log("PASS: isPromise(Promise.resolve()) = true");
        } else {
            console.log("FAIL: isPromise(Promise.resolve()) should be true");
            failures++;
        }
    }

    // Test 2: new Promise is a Promise
    {
        const p = new Promise<void>((resolve) => resolve());
        if (util.types.isPromise(p)) {
            console.log("PASS: isPromise(new Promise(...)) = true");
        } else {
            console.log("FAIL: isPromise(new Promise(...)) should be true");
            failures++;
        }
    }

    // Test 3: Promise.reject() returns a Promise
    {
        const p = Promise.reject(new Error("test"));
        // Catch to prevent unhandled rejection
        p.catch(() => {});
        if (util.types.isPromise(p)) {
            console.log("PASS: isPromise(Promise.reject()) = true");
        } else {
            console.log("FAIL: isPromise(Promise.reject()) should be true");
            failures++;
        }
    }

    // Test 4: Plain object is not a Promise
    {
        const obj = { then: () => {} };
        if (!util.types.isPromise(obj)) {
            console.log("PASS: isPromise({ then: () => {} }) = false");
        } else {
            console.log("FAIL: isPromise({ then: () => {} }) should be false");
            failures++;
        }
    }

    // Test 5: null is not a Promise
    {
        if (!util.types.isPromise(null)) {
            console.log("PASS: isPromise(null) = false");
        } else {
            console.log("FAIL: isPromise(null) should be false");
            failures++;
        }
    }

    // Test 6: undefined is not a Promise
    {
        if (!util.types.isPromise(undefined)) {
            console.log("PASS: isPromise(undefined) = false");
        } else {
            console.log("FAIL: isPromise(undefined) should be false");
            failures++;
        }
    }

    // Test 7: string is not a Promise
    {
        if (!util.types.isPromise("promise")) {
            console.log("PASS: isPromise('promise') = false");
        } else {
            console.log("FAIL: isPromise('promise') should be false");
            failures++;
        }
    }

    // Test 8: number is not a Promise
    {
        if (!util.types.isPromise(42)) {
            console.log("PASS: isPromise(42) = false");
        } else {
            console.log("FAIL: isPromise(42) should be false");
            failures++;
        }
    }

    // Test 9: Array is not a Promise
    {
        if (!util.types.isPromise([1, 2, 3])) {
            console.log("PASS: isPromise([1,2,3]) = false");
        } else {
            console.log("FAIL: isPromise([1,2,3]) should be false");
            failures++;
        }
    }

    // Test 10: Date is not a Promise
    {
        if (!util.types.isPromise(new Date())) {
            console.log("PASS: isPromise(new Date()) = false");
        } else {
            console.log("FAIL: isPromise(new Date()) should be false");
            failures++;
        }
    }

    console.log("");
    console.log("=== Summary ===");
    if (failures === 0) {
        console.log("All tests passed!");
    } else {
        console.log("Failures: " + failures);
    }

    return failures;
}
