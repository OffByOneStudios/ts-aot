// Test util.types functions that were fixed
import * as util from 'util';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    console.log("=== util.types Tests (Fixed Functions) ===");

    // Test 1: isDate
    {
        console.log("\nTest 1: util.types.isDate()");
        const date = new Date();
        const notDate = { getTime: () => 123 };

        if (util.types.isDate(date)) {
            console.log("PASS: isDate(new Date()) = true");
            passed++;
        } else {
            console.log("FAIL: isDate(new Date()) should be true");
            failed++;
        }

        if (!util.types.isDate(notDate)) {
            console.log("PASS: isDate({}) = false");
            passed++;
        } else {
            console.log("FAIL: isDate({}) should be false");
            failed++;
        }
    }

    // Test 2: isRegExp
    {
        console.log("\nTest 2: util.types.isRegExp()");
        const regex = /hello/;
        const notRegex = "hello";

        if (util.types.isRegExp(regex)) {
            console.log("PASS: isRegExp(/hello/) = true");
            passed++;
        } else {
            console.log("FAIL: isRegExp(/hello/) should be true");
            failed++;
        }

        if (!util.types.isRegExp(notRegex)) {
            console.log("PASS: isRegExp('hello') = false");
            passed++;
        } else {
            console.log("FAIL: isRegExp('hello') should be false");
            failed++;
        }
    }

    // Test 3: isNativeError
    {
        console.log("\nTest 3: util.types.isNativeError()");
        const err = new Error("test error");
        const notErr = { message: "fake error" };

        if (util.types.isNativeError(err)) {
            console.log("PASS: isNativeError(new Error()) = true");
            passed++;
        } else {
            console.log("FAIL: isNativeError(new Error()) should be true");
            failed++;
        }

        if (!util.types.isNativeError(notErr)) {
            console.log("PASS: isNativeError({}) = false");
            passed++;
        } else {
            console.log("FAIL: isNativeError({}) should be false");
            failed++;
        }
    }

    // Test 4: isSet (should already work)
    {
        console.log("\nTest 4: util.types.isSet()");
        const set = new Set();
        const notSet = [1, 2, 3];

        if (util.types.isSet(set)) {
            console.log("PASS: isSet(new Set()) = true");
            passed++;
        } else {
            console.log("FAIL: isSet(new Set()) should be true");
            failed++;
        }

        if (!util.types.isSet(notSet)) {
            console.log("PASS: isSet([]) = false");
            passed++;
        } else {
            console.log("FAIL: isSet([]) should be false");
            failed++;
        }
    }

    // Print summary
    console.log("");
    console.log("=== util.types Results ===");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);
    console.log("Total: " + (passed + failed));

    return failed;
}
