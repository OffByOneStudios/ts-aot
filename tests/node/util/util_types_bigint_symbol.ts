// Test util.types.isBigInt64Array() and isBigUint64Array()
import * as util from 'util';

function user_main(): number {
    console.log("=== util.types BigInt Array Tests ===");
    console.log("");
    let failures = 0;

    // Test 1: isBigInt64Array with BigInt64Array
    const bigInt64Arr = new BigInt64Array(4);
    const result1 = util.types.isBigInt64Array(bigInt64Arr);
    if (result1 === true) {
        console.log("PASS: BigInt64Array detected as BigInt64Array");
    } else {
        console.log("FAIL: Expected true for BigInt64Array, got: " + result1);
        failures++;
    }

    // Test 2: isBigInt64Array with regular Int32Array (should be false)
    const int32Arr = new Int32Array(4);
    const result2 = util.types.isBigInt64Array(int32Arr);
    if (result2 === false) {
        console.log("PASS: Int32Array NOT detected as BigInt64Array");
    } else {
        console.log("FAIL: Expected false for Int32Array, got: " + result2);
        failures++;
    }

    // Test 3: isBigUint64Array with BigUint64Array
    const bigUint64Arr = new BigUint64Array(4);
    const result3 = util.types.isBigUint64Array(bigUint64Arr);
    if (result3 === true) {
        console.log("PASS: BigUint64Array detected as BigUint64Array");
    } else {
        console.log("FAIL: Expected true for BigUint64Array, got: " + result3);
        failures++;
    }

    // Test 4: isBigUint64Array with BigInt64Array (should be false)
    const result4 = util.types.isBigUint64Array(bigInt64Arr);
    if (result4 === false) {
        console.log("PASS: BigInt64Array NOT detected as BigUint64Array");
    } else {
        console.log("FAIL: Expected false for BigInt64Array, got: " + result4);
        failures++;
    }

    // Test 5: isBigInt64Array with plain object (should be false)
    const plainObj = { foo: "bar" };
    const result5 = util.types.isBigInt64Array(plainObj);
    if (result5 === false) {
        console.log("PASS: Plain object NOT detected as BigInt64Array");
    } else {
        console.log("FAIL: Expected false for plain object, got: " + result5);
        failures++;
    }

    // Test 6: isBigUint64Array with Float64Array (should be false)
    const float64Arr = new Float64Array(4);
    const result6 = util.types.isBigUint64Array(float64Arr);
    if (result6 === false) {
        console.log("PASS: Float64Array NOT detected as BigUint64Array");
    } else {
        console.log("FAIL: Expected false for Float64Array, got: " + result6);
        failures++;
    }

    // Test 7: isBigInt64Array with Float64Array (should be false)
    const result7 = util.types.isBigInt64Array(float64Arr);
    if (result7 === false) {
        console.log("PASS: Float64Array NOT detected as BigInt64Array");
    } else {
        console.log("FAIL: Expected false for Float64Array, got: " + result7);
        failures++;
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
