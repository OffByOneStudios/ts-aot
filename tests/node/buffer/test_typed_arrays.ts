// Test TypedArray implementations

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Int8Array creation
    console.log("Test 1 - Int8Array creation:");
    const i8 = new Int8Array(4);
    if (i8.length === 4) {
        console.log("PASS: Int8Array length correct");
        passed++;
    } else {
        console.log("FAIL: Int8Array length incorrect");
        failed++;
    }

    // Test 2: Int8Array BYTES_PER_ELEMENT
    console.log("Test 2 - Int8Array BYTES_PER_ELEMENT:");
    if (i8.BYTES_PER_ELEMENT === 1) {
        console.log("PASS: Int8Array BYTES_PER_ELEMENT is 1");
        passed++;
    } else {
        console.log("FAIL: Int8Array BYTES_PER_ELEMENT incorrect");
        failed++;
    }

    // Test 3: Int16Array creation
    console.log("Test 3 - Int16Array creation:");
    const i16 = new Int16Array(4);
    if (i16.length === 4 && i16.BYTES_PER_ELEMENT === 2) {
        console.log("PASS: Int16Array correct");
        passed++;
    } else {
        console.log("FAIL: Int16Array incorrect");
        failed++;
    }

    // Test 4: Int32Array creation
    console.log("Test 4 - Int32Array creation:");
    const i32 = new Int32Array(4);
    if (i32.length === 4 && i32.BYTES_PER_ELEMENT === 4) {
        console.log("PASS: Int32Array correct");
        passed++;
    } else {
        console.log("FAIL: Int32Array incorrect");
        failed++;
    }

    // Test 5: Uint8Array creation
    console.log("Test 5 - Uint8Array creation:");
    const u8 = new Uint8Array(4);
    if (u8.length === 4 && u8.BYTES_PER_ELEMENT === 1) {
        console.log("PASS: Uint8Array correct");
        passed++;
    } else {
        console.log("FAIL: Uint8Array incorrect");
        failed++;
    }

    // Test 6: Uint16Array creation
    console.log("Test 6 - Uint16Array creation:");
    const u16 = new Uint16Array(4);
    if (u16.length === 4 && u16.BYTES_PER_ELEMENT === 2) {
        console.log("PASS: Uint16Array correct");
        passed++;
    } else {
        console.log("FAIL: Uint16Array incorrect");
        failed++;
    }

    // Test 7: Uint32Array creation
    console.log("Test 7 - Uint32Array creation:");
    const u32 = new Uint32Array(4);
    if (u32.length === 4 && u32.BYTES_PER_ELEMENT === 4) {
        console.log("PASS: Uint32Array correct");
        passed++;
    } else {
        console.log("FAIL: Uint32Array incorrect");
        failed++;
    }

    // Test 8: Float32Array creation
    console.log("Test 8 - Float32Array creation:");
    const f32 = new Float32Array(4);
    if (f32.length === 4 && f32.BYTES_PER_ELEMENT === 4) {
        console.log("PASS: Float32Array correct");
        passed++;
    } else {
        console.log("FAIL: Float32Array incorrect");
        failed++;
    }

    // Test 9: Float64Array creation
    console.log("Test 9 - Float64Array creation:");
    const f64 = new Float64Array(4);
    if (f64.length === 4 && f64.BYTES_PER_ELEMENT === 8) {
        console.log("PASS: Float64Array correct");
        passed++;
    } else {
        console.log("FAIL: Float64Array incorrect");
        failed++;
    }

    // Test 10: byteLength property
    console.log("Test 10 - byteLength property:");
    if (i8.byteLength === 4 && i16.byteLength === 8 && i32.byteLength === 16) {
        console.log("PASS: byteLength correct for all types");
        passed++;
    } else {
        console.log("FAIL: byteLength incorrect");
        console.log("i8.byteLength:", i8.byteLength);
        console.log("i16.byteLength:", i16.byteLength);
        console.log("i32.byteLength:", i32.byteLength);
        failed++;
    }

    console.log("");
    console.log("Results:", passed, "passed,", failed, "failed");

    return failed > 0 ? 1 : 0;
}
