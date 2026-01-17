// Test util.types.is* for TypedArrays - tests what's implemented
import * as util from 'util';

let passed = 0;
let failed = 0;

function test(name: string, condition: boolean): void {
    if (condition) {
        console.log("PASS: " + name);
        passed++;
    } else {
        console.log("FAIL: " + name);
        failed++;
    }
}

function user_main(): number {
    console.log("=== Testing util.types.is* for TypedArrays ===\n");

    // Create instances of each TypedArray type
    const int16 = new Int16Array(4);
    const int32 = new Int32Array(4);
    const uint8 = new Uint8Array(4);
    const uint8c = new Uint8ClampedArray(4);
    const uint16 = new Uint16Array(4);
    const uint32 = new Uint32Array(4);
    const float64 = new Float64Array(4);

    // Test isUint8Array (works with Buffer)
    test("Uint8Array isUint8Array", util.types.isUint8Array(uint8) === true);
    test("Uint8ClampedArray is NOT Uint8Array", util.types.isUint8Array(uint8c) === false);

    // Test isUint8ClampedArray
    test("Uint8ClampedArray isUint8ClampedArray", util.types.isUint8ClampedArray(uint8c) === true);
    test("Uint8Array is NOT Uint8ClampedArray", util.types.isUint8ClampedArray(uint8) === false);

    // Test size-based detection (works by element size)
    test("Int16Array has 2-byte elements", util.types.isInt16Array(int16) === true);
    test("Uint16Array has 2-byte elements", util.types.isUint16Array(uint16) === true);
    test("Int32Array has 4-byte elements", util.types.isInt32Array(int32) === true);
    test("Uint32Array has 4-byte elements", util.types.isUint32Array(uint32) === true);
    test("Float64Array has 8-byte elements", util.types.isFloat64Array(float64) === true);

    // Test that all typed arrays are TypedArray
    test("Uint8Array is TypedArray", util.types.isTypedArray(uint8) === true);
    test("Float64Array is TypedArray", util.types.isTypedArray(float64) === true);

    // Test negative cases
    const arr: number[] = [1, 2, 3];
    test("Array is NOT TypedArray", util.types.isTypedArray(arr) === false);
    test("Array is NOT Uint8Array", util.types.isUint8Array(arr) === false);

    console.log("\n=== Summary ===");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    // Note: Some is* functions don't distinguish signed/unsigned or float/int
    // because TsTypedArray doesn't track these distinctions.
    // isFloat32Array returns false, isDataView returns false
    // isInt8Array can't distinguish from isUint8Array (both are 1-byte non-clamped)

    return failed > 0 ? 1 : 0;
}
