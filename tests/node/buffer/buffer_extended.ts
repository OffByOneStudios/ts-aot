// Test Buffer slice, subarray, fill, copy, and byteLength

function user_main(): number {
    console.log("=== Buffer Extended Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: Buffer.slice
    console.log("\n1. Buffer.slice:");
    const buf1 = Buffer.from("Hello World");
    const sliced = buf1.slice(0, 5);
    const slicedStr = sliced.toString();
    console.log("  Buffer.from('Hello World').slice(0, 5) = '" + slicedStr + "'");
    if (slicedStr === "Hello") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 2: Buffer.subarray
    console.log("\n2. Buffer.subarray:");
    const subarr = buf1.subarray(6, 11);
    const subarrStr = subarr.toString();
    console.log("  Buffer.from('Hello World').subarray(6, 11) = '" + subarrStr + "'");
    if (subarrStr === "World") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 3: Buffer.fill
    console.log("\n3. Buffer.fill:");
    const fillBuf = Buffer.alloc(5);
    fillBuf.fill(65); // ASCII 'A'
    const fillStr = fillBuf.toString();
    console.log("  Buffer.alloc(5).fill(65) = '" + fillStr + "'");
    if (fillStr === "AAAAA") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 'AAAAA')");
        failed++;
    }

    // Test 4: Buffer.copy
    console.log("\n4. Buffer.copy:");
    const srcBuf = Buffer.from("Hello");
    const destBuf = Buffer.alloc(10);
    destBuf.fill(45); // Fill with '-'
    srcBuf.copy(destBuf, 2);
    const copyStr = destBuf.toString();
    console.log("  Copied 'Hello' to dest at offset 2: '" + copyStr + "'");
    // Should be "--Hello---"
    if (copyStr.indexOf("Hello") === 2) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 5: buf.length (byteLength)
    console.log("\n5. Buffer.length:");
    const lenBuf = Buffer.from("Test");
    const len = lenBuf.length;
    console.log("  Buffer.from('Test').length = " + len);
    if (len === 4) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");
    if (failed === 0) {
        console.log("All Buffer extended tests passed!");
    }

    return failed;
}
