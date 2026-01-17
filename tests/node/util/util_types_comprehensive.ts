// Comprehensive test for util.types functions
import * as util from 'util';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    console.log("=== util.types Comprehensive Tests ===");

    // Test 1: isPromise
    {
        console.log("\nTest 1: util.types.isPromise()");
        if (util.types.isPromise(Promise.resolve(42))) {
            console.log("PASS: isPromise(Promise.resolve) = true");
            passed++;
        } else {
            console.log("FAIL: isPromise(Promise.resolve) should be true");
            failed++;
        }

        if (!util.types.isPromise({})) {
            console.log("PASS: isPromise({}) = false");
            passed++;
        } else {
            console.log("FAIL: isPromise({}) should be false");
            failed++;
        }
    }

    // Test 2: isDate
    {
        console.log("\nTest 2: util.types.isDate()");
        const date = new Date();
        if (util.types.isDate(date)) {
            console.log("PASS: isDate(new Date()) = true");
            passed++;
        } else {
            console.log("FAIL: isDate(new Date()) should be true");
            failed++;
        }

        if (!util.types.isDate(Date.now())) {
            console.log("PASS: isDate(Date.now()) = false");
            passed++;
        } else {
            console.log("FAIL: isDate(Date.now()) should be false");
            failed++;
        }
    }

    // Test 3: isRegExp
    {
        console.log("\nTest 3: util.types.isRegExp()");
        if (util.types.isRegExp(/test/)) {
            console.log("PASS: isRegExp(/test/) = true");
            passed++;
        } else {
            console.log("FAIL: isRegExp(/test/) should be true");
            failed++;
        }

        if (util.types.isRegExp(new RegExp("test"))) {
            console.log("PASS: isRegExp(new RegExp('test')) = true");
            passed++;
        } else {
            console.log("FAIL: isRegExp(new RegExp('test')) should be true");
            failed++;
        }

        if (!util.types.isRegExp("test")) {
            console.log("PASS: isRegExp('test') = false");
            passed++;
        } else {
            console.log("FAIL: isRegExp('test') should be false");
            failed++;
        }
    }

    // Test 4: isNativeError
    {
        console.log("\nTest 4: util.types.isNativeError()");
        if (util.types.isNativeError(new Error("test"))) {
            console.log("PASS: isNativeError(new Error()) = true");
            passed++;
        } else {
            console.log("FAIL: isNativeError(new Error()) should be true");
            failed++;
        }

        if (util.types.isNativeError(new TypeError("test"))) {
            console.log("PASS: isNativeError(new TypeError()) = true");
            passed++;
        } else {
            console.log("FAIL: isNativeError(new TypeError()) should be true");
            failed++;
        }

        if (!util.types.isNativeError({ message: "error" })) {
            console.log("PASS: isNativeError({message}) = false");
            passed++;
        } else {
            console.log("FAIL: isNativeError({message}) should be false");
            failed++;
        }
    }

    // Test 5: isSet
    {
        console.log("\nTest 5: util.types.isSet()");
        if (util.types.isSet(new Set())) {
            console.log("PASS: isSet(new Set()) = true");
            passed++;
        } else {
            console.log("FAIL: isSet(new Set()) should be true");
            failed++;
        }

        if (!util.types.isSet([])) {
            console.log("PASS: isSet([]) = false");
            passed++;
        } else {
            console.log("FAIL: isSet([]) should be false");
            failed++;
        }
    }

    // Test 6: isMap
    {
        console.log("\nTest 6: util.types.isMap()");
        if (util.types.isMap(new Map())) {
            console.log("PASS: isMap(new Map()) = true");
            passed++;
        } else {
            console.log("FAIL: isMap(new Map()) should be true");
            failed++;
        }

        if (!util.types.isMap({})) {
            console.log("PASS: isMap({}) = false");
            passed++;
        } else {
            console.log("FAIL: isMap({}) should be false");
            failed++;
        }
    }

    // Test 7: isTypedArray
    {
        console.log("\nTest 7: util.types.isTypedArray()");
        if (util.types.isTypedArray(new Uint8Array(4))) {
            console.log("PASS: isTypedArray(new Uint8Array(4)) = true");
            passed++;
        } else {
            console.log("FAIL: isTypedArray(new Uint8Array(4)) should be true");
            failed++;
        }

        if (!util.types.isTypedArray([])) {
            console.log("PASS: isTypedArray([]) = false");
            passed++;
        } else {
            console.log("FAIL: isTypedArray([]) should be false");
            failed++;
        }
    }

    // Test 8: isUint8Array
    {
        console.log("\nTest 8: util.types.isUint8Array()");
        if (util.types.isUint8Array(new Uint8Array(4))) {
            console.log("PASS: isUint8Array(new Uint8Array(4)) = true");
            passed++;
        } else {
            console.log("FAIL: isUint8Array(new Uint8Array(4)) should be true");
            failed++;
        }

        if (!util.types.isUint8Array(new Float64Array(4))) {
            console.log("PASS: isUint8Array(new Float64Array(4)) = false");
            passed++;
        } else {
            console.log("FAIL: isUint8Array(new Float64Array(4)) should be false");
            failed++;
        }
    }

    // Test 9: isFloat64Array
    {
        console.log("\nTest 9: util.types.isFloat64Array()");
        if (util.types.isFloat64Array(new Float64Array(4))) {
            console.log("PASS: isFloat64Array(new Float64Array(4)) = true");
            passed++;
        } else {
            console.log("FAIL: isFloat64Array(new Float64Array(4)) should be true");
            failed++;
        }

        if (!util.types.isFloat64Array(new Uint8Array(4))) {
            console.log("PASS: isFloat64Array(new Uint8Array(4)) = false");
            passed++;
        } else {
            console.log("FAIL: isFloat64Array(new Uint8Array(4)) should be false");
            failed++;
        }
    }

    // Test 10: isDataView
    {
        console.log("\nTest 10: util.types.isDataView()");
        const buffer = new ArrayBuffer(16);
        const dv = new DataView(buffer);
        if (util.types.isDataView(dv)) {
            console.log("PASS: isDataView(new DataView(...)) = true");
            passed++;
        } else {
            console.log("FAIL: isDataView(new DataView(...)) should be true");
            failed++;
        }

        if (!util.types.isDataView(buffer)) {
            console.log("PASS: isDataView(ArrayBuffer) = false");
            passed++;
        } else {
            console.log("FAIL: isDataView(ArrayBuffer) should be false");
            failed++;
        }
    }

    // Test 11: isUint8ClampedArray
    {
        console.log("\nTest 11: util.types.isUint8ClampedArray()");
        if (util.types.isUint8ClampedArray(new Uint8ClampedArray(4))) {
            console.log("PASS: isUint8ClampedArray(new Uint8ClampedArray(4)) = true");
            passed++;
        } else {
            console.log("FAIL: isUint8ClampedArray(new Uint8ClampedArray(4)) should be true");
            failed++;
        }

        if (!util.types.isUint8ClampedArray(new Uint8Array(4))) {
            console.log("PASS: isUint8ClampedArray(new Uint8Array(4)) = false");
            passed++;
        } else {
            console.log("FAIL: isUint8ClampedArray(new Uint8Array(4)) should be false");
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
