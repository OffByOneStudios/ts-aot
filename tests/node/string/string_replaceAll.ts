// Test String.prototype.replaceAll()

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Replace all occurrences of a word
    const str1 = "hello world world";
    const result1 = str1.replaceAll("world", "there");
    if (result1 === "hello there there") {
        console.log("PASS: replaceAll() replaces all occurrences");
        passed++;
    } else {
        console.log("FAIL: replaceAll() - expected 'hello there there', got '" + result1 + "'");
        failed++;
    }

    // Test 2: Replace single character
    const str2 = "a-b-c-d";
    const result2 = str2.replaceAll("-", "_");
    if (result2 === "a_b_c_d") {
        console.log("PASS: replaceAll() single char replacement");
        passed++;
    } else {
        console.log("FAIL: replaceAll() single char - expected 'a_b_c_d', got '" + result2 + "'");
        failed++;
    }

    // Test 3: No matches - string unchanged
    const str3 = "hello world";
    const result3 = str3.replaceAll("xyz", "abc");
    if (result3 === "hello world") {
        console.log("PASS: replaceAll() with no matches");
        passed++;
    } else {
        console.log("FAIL: replaceAll() no matches - expected 'hello world', got '" + result3 + "'");
        failed++;
    }

    // Test 4: Replace with empty string (deletion)
    const str4 = "a1b2c3";
    const result4 = str4.replaceAll("1", "").replaceAll("2", "").replaceAll("3", "");
    if (result4 === "abc") {
        console.log("PASS: replaceAll() deletion");
        passed++;
    } else {
        console.log("FAIL: replaceAll() deletion - expected 'abc', got '" + result4 + "'");
        failed++;
    }

    // Test 5: Empty string input
    const str5 = "";
    const result5 = str5.replaceAll("x", "y");
    if (result5 === "") {
        console.log("PASS: replaceAll() on empty string");
        passed++;
    } else {
        console.log("FAIL: replaceAll() empty - expected '', got '" + result5 + "'");
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
