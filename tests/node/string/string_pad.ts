// Test String.prototype.padStart() and padEnd()

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: padStart with zeros
    const str1 = "5";
    const result1 = str1.padStart(3, "0");
    if (result1 === "005") {
        console.log("PASS: padStart('5', 3, '0') = '005'");
        passed++;
    } else {
        console.log("FAIL: padStart - expected '005', got '" + result1 + "'");
        failed++;
    }

    // Test 2: padEnd with zeros
    const str2 = "5";
    const result2 = str2.padEnd(3, "0");
    if (result2 === "500") {
        console.log("PASS: padEnd('5', 3, '0') = '500'");
        passed++;
    } else {
        console.log("FAIL: padEnd - expected '500', got '" + result2 + "'");
        failed++;
    }

    // Test 3: padStart when string is already long enough
    const str3 = "hello";
    const result3 = str3.padStart(3, "x");
    if (result3 === "hello") {
        console.log("PASS: padStart no change when long enough");
        passed++;
    } else {
        console.log("FAIL: padStart long - expected 'hello', got '" + result3 + "'");
        failed++;
    }

    // Test 4: padEnd when string is already long enough
    const str4 = "hello";
    const result4 = str4.padEnd(3, "x");
    if (result4 === "hello") {
        console.log("PASS: padEnd no change when long enough");
        passed++;
    } else {
        console.log("FAIL: padEnd long - expected 'hello', got '" + result4 + "'");
        failed++;
    }

    // Test 5: padStart with spaces (default)
    const str5 = "abc";
    const result5 = str5.padStart(6, " ");
    if (result5 === "   abc") {
        console.log("PASS: padStart with spaces");
        passed++;
    } else {
        console.log("FAIL: padStart spaces - expected '   abc', got '" + result5 + "'");
        failed++;
    }

    // Test 6: padEnd with spaces
    const str6 = "abc";
    const result6 = str6.padEnd(6, " ");
    if (result6 === "abc   ") {
        console.log("PASS: padEnd with spaces");
        passed++;
    } else {
        console.log("FAIL: padEnd spaces - expected 'abc   ', got '" + result6 + "'");
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
