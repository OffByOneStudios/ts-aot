// Test String.trimStart() and String.trimEnd()

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: trimStart() removes leading whitespace
    const str1 = "   hello   ";
    if (str1.trimStart() === "hello   ") {
        console.log("PASS: trimStart() removes leading whitespace");
        passed++;
    } else {
        console.log("FAIL: trimStart() - got '" + str1.trimStart() + "'");
        failed++;
    }

    // Test 2: trimEnd() removes trailing whitespace
    if (str1.trimEnd() === "   hello") {
        console.log("PASS: trimEnd() removes trailing whitespace");
        passed++;
    } else {
        console.log("FAIL: trimEnd() - got '" + str1.trimEnd() + "'");
        failed++;
    }

    // Test 3: trimStart() with tabs
    const str2 = "\t\ttext";
    if (str2.trimStart() === "text") {
        console.log("PASS: trimStart() removes tabs");
        passed++;
    } else {
        console.log("FAIL: trimStart() with tabs - got '" + str2.trimStart() + "'");
        failed++;
    }

    // Test 4: trimEnd() with newlines
    const str3 = "text\n\n";
    if (str3.trimEnd() === "text") {
        console.log("PASS: trimEnd() removes newlines");
        passed++;
    } else {
        console.log("FAIL: trimEnd() with newlines - got '" + str3.trimEnd() + "'");
        failed++;
    }

    // Test 5: trimStart() on string with no leading whitespace
    const str4 = "hello   ";
    if (str4.trimStart() === "hello   ") {
        console.log("PASS: trimStart() no-op when no leading whitespace");
        passed++;
    } else {
        console.log("FAIL: trimStart() on no-leading - got '" + str4.trimStart() + "'");
        failed++;
    }

    // Test 6: trimEnd() on string with no trailing whitespace
    const str5 = "   hello";
    if (str5.trimEnd() === "   hello") {
        console.log("PASS: trimEnd() no-op when no trailing whitespace");
        passed++;
    } else {
        console.log("FAIL: trimEnd() on no-trailing - got '" + str5.trimEnd() + "'");
        failed++;
    }

    // Test 7: Mixed whitespace characters
    const str6 = " \t\n  text  \t\n ";
    if (str6.trimStart() === "text  \t\n ") {
        console.log("PASS: trimStart() handles mixed whitespace");
        passed++;
    } else {
        console.log("FAIL: trimStart() mixed whitespace - got '" + str6.trimStart() + "'");
        failed++;
    }

    if (str6.trimEnd() === " \t\n  text") {
        console.log("PASS: trimEnd() handles mixed whitespace");
        passed++;
    } else {
        console.log("FAIL: trimEnd() mixed whitespace - got '" + str6.trimEnd() + "'");
        failed++;
    }

    // Test 9: Empty string
    const str7 = "";
    if (str7.trimStart() === "") {
        console.log("PASS: trimStart() on empty string");
        passed++;
    } else {
        console.log("FAIL: trimStart() on empty string - got '" + str7.trimStart() + "'");
        failed++;
    }

    if (str7.trimEnd() === "") {
        console.log("PASS: trimEnd() on empty string");
        passed++;
    } else {
        console.log("FAIL: trimEnd() on empty string - got '" + str7.trimEnd() + "'");
        failed++;
    }

    // Test 11: All whitespace string
    const str8 = "   \t\n   ";
    if (str8.trimStart() === "") {
        console.log("PASS: trimStart() on all whitespace");
        passed++;
    } else {
        console.log("FAIL: trimStart() on all whitespace - got '" + str8.trimStart() + "'");
        failed++;
    }

    if (str8.trimEnd() === "") {
        console.log("PASS: trimEnd() on all whitespace");
        passed++;
    } else {
        console.log("FAIL: trimEnd() on all whitespace - got '" + str8.trimEnd() + "'");
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
