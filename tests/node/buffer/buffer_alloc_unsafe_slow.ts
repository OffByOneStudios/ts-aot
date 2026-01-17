// Test Buffer.allocUnsafeSlow

function user_main(): number {
    console.log("Testing Buffer.allocUnsafeSlow...");

    // Test basic allocation
    console.log("Test 1: Basic allocation");
    const buf1 = Buffer.allocUnsafeSlow(10);
    console.log("  Length: " + buf1.length);
    if (buf1.length !== 10) {
        console.log("FAIL: Expected length 10");
        return 1;
    }
    console.log("  Buffer.allocUnsafeSlow(10) works");

    // Test zero-length buffer
    console.log("Test 2: Zero-length buffer");
    const buf2 = Buffer.allocUnsafeSlow(0);
    console.log("  Length: " + buf2.length);
    if (buf2.length !== 0) {
        console.log("FAIL: Expected length 0");
        return 1;
    }
    console.log("  Buffer.allocUnsafeSlow(0) works");

    // Test larger buffer
    console.log("Test 3: Larger buffer");
    const buf3 = Buffer.allocUnsafeSlow(1024);
    console.log("  Length: " + buf3.length);
    if (buf3.length !== 1024) {
        console.log("FAIL: Expected length 1024");
        return 1;
    }
    console.log("  Buffer.allocUnsafeSlow(1024) works");

    // Test that we can write to the buffer
    console.log("Test 4: Write to buffer");
    buf1.writeUInt8(42, 0);
    buf1.writeUInt8(255, 9);
    const val0 = buf1.readUInt8(0);
    const val9 = buf1.readUInt8(9);
    console.log("  Value at 0: " + val0);
    console.log("  Value at 9: " + val9);
    if (val0 !== 42 || val9 !== 255) {
        console.log("FAIL: Read/write mismatch");
        return 1;
    }
    console.log("  Write/read works");

    // Test isBuffer
    console.log("Test 5: isBuffer check");
    if (!Buffer.isBuffer(buf1)) {
        console.log("FAIL: Buffer.isBuffer should return true");
        return 1;
    }
    console.log("  Buffer.isBuffer works");

    console.log("All Buffer.allocUnsafeSlow tests passed!");
    return 0;
}
