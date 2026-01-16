// Test util.types.isTypedArray() and isUint8Array() with Buffer and TypedArrays
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
    // Test 1: Buffer should be a TypedArray
    const buf = Buffer.alloc(10);
    test("Buffer is TypedArray", util.types.isTypedArray(buf) === true);

    // Test 2: Buffer should be a Uint8Array
    test("Buffer is Uint8Array", util.types.isUint8Array(buf) === true);

    // Test 3: Uint8Array should be TypedArray
    const u8 = new Uint8Array(10);
    test("Uint8Array is TypedArray", util.types.isTypedArray(u8) === true);

    // Test 4: Uint8Array should be Uint8Array
    test("Uint8Array is Uint8Array", util.types.isUint8Array(u8) === true);

    // Test 5: Uint32Array should be TypedArray
    const u32 = new Uint32Array(10);
    test("Uint32Array is TypedArray", util.types.isTypedArray(u32) === true);

    // Test 6: Float64Array should be TypedArray
    const f64 = new Float64Array(10);
    test("Float64Array is TypedArray", util.types.isTypedArray(f64) === true);

    // Test 7: Regular array should NOT be TypedArray
    const arr = [1, 2, 3];
    test("Array is not TypedArray", util.types.isTypedArray(arr) === false);

    // Test 8: Object should NOT be TypedArray
    const obj = { a: 1 };
    test("Object is not TypedArray", util.types.isTypedArray(obj) === false);

    // Test 9: null should NOT be TypedArray
    test("null is not TypedArray", util.types.isTypedArray(null) === false);

    // Test 10: isArrayBuffer with Buffer
    test("Buffer isArrayBuffer", util.types.isArrayBuffer(buf) === true);

    // Test 11: isArrayBufferView with Buffer
    test("Buffer isArrayBufferView", util.types.isArrayBufferView(buf) === true);

    console.log("\n" + passed + " passed, " + failed + " failed");
    return failed > 0 ? 1 : 0;
}
