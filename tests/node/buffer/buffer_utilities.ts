// Test Buffer.isBuffer() and Buffer.concat()

function user_main(): number {
    console.log("=== Buffer Utility Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: Buffer.isBuffer with actual buffer
    console.log("\n1. Buffer.isBuffer with Buffer:");
    const buf1 = Buffer.from("hello");
    const result1 = Buffer.isBuffer(buf1);
    console.log("  Buffer.isBuffer(Buffer.from('hello')) = " + result1);
    if (result1 === true) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 2: Buffer.isBuffer with non-buffer
    console.log("\n2. Buffer.isBuffer with string:");
    const result2 = Buffer.isBuffer("not a buffer");
    console.log("  Buffer.isBuffer('not a buffer') = " + result2);
    if (result2 === false) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 3: Buffer.isBuffer with number
    console.log("\n3. Buffer.isBuffer with number:");
    const result3 = Buffer.isBuffer(42);
    console.log("  Buffer.isBuffer(42) = " + result3);
    if (result3 === false) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 4: Buffer.isBuffer with null
    console.log("\n4. Buffer.isBuffer with null:");
    const result4 = Buffer.isBuffer(null);
    console.log("  Buffer.isBuffer(null) = " + result4);
    if (result4 === false) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 5: Buffer.concat with two buffers
    console.log("\n5. Buffer.concat with two buffers:");
    const bufA = Buffer.from("Hello");
    const bufB = Buffer.from("World");
    const combined = Buffer.concat([bufA, bufB]);
    const combinedStr = combined.toString();
    console.log("  Buffer.concat(['Hello', 'World']) = '" + combinedStr + "'");
    if (combinedStr === "HelloWorld") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 'HelloWorld')");
        failed++;
    }

    // Test 6: Buffer.concat with three buffers
    console.log("\n6. Buffer.concat with three buffers:");
    const buf1a = Buffer.from("a");
    const buf2a = Buffer.from("b");
    const buf3a = Buffer.from("c");
    const abc = Buffer.concat([buf1a, buf2a, buf3a]);
    const abcStr = abc.toString();
    console.log("  Buffer.concat(['a', 'b', 'c']) = '" + abcStr + "'");
    if (abcStr === "abc") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 'abc')");
        failed++;
    }

    // Test 7: Buffer.concat length check
    console.log("\n7. Buffer.concat length:");
    const len = combined.length;
    console.log("  combined.length = " + len);
    if (len === 10) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 10)");
        failed++;
    }

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");
    if (failed === 0) {
        console.log("All Buffer utility tests passed!");
    }

    return failed;
}
