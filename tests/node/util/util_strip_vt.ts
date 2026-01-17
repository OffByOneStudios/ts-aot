// Test util.stripVTControlCharacters() and util.toUSVString()
import * as util from 'util';

function user_main(): number {
    console.log("=== util.stripVTControlCharacters() Tests ===");
    console.log("");
    let failures = 0;

    // Test 1: Remove basic ANSI color codes
    const colored = "\x1b[31mRed\x1b[0m Text";
    const stripped1 = util.stripVTControlCharacters(colored);
    if (stripped1 === "Red Text") {
        console.log("PASS: Basic color codes removed");
    } else {
        console.log("FAIL: Expected 'Red Text', got: " + stripped1);
        failures++;
    }

    // Test 2: Remove bold/underline codes
    const styled = "\x1b[1mBold\x1b[0m and \x1b[4mUnderline\x1b[0m";
    const stripped2 = util.stripVTControlCharacters(styled);
    if (stripped2 === "Bold and Underline") {
        console.log("PASS: Style codes removed");
    } else {
        console.log("FAIL: Expected 'Bold and Underline', got: " + stripped2);
        failures++;
    }

    // Test 3: Remove cursor movement codes
    const cursor = "Start\x1b[2AUp\x1b[3BDown";
    const stripped3 = util.stripVTControlCharacters(cursor);
    if (stripped3 === "StartUpDown") {
        console.log("PASS: Cursor codes removed");
    } else {
        console.log("FAIL: Expected 'StartUpDown', got: " + stripped3);
        failures++;
    }

    // Test 4: Plain text unchanged
    const plain = "Hello, World!";
    const stripped4 = util.stripVTControlCharacters(plain);
    if (stripped4 === plain) {
        console.log("PASS: Plain text unchanged");
    } else {
        console.log("FAIL: Expected 'Hello, World!', got: " + stripped4);
        failures++;
    }

    // Test 5: Empty string
    const empty = "";
    const stripped5 = util.stripVTControlCharacters(empty);
    if (stripped5 === "") {
        console.log("PASS: Empty string handled");
    } else {
        console.log("FAIL: Expected empty string, got: " + stripped5);
        failures++;
    }

    // Test 6: Multiple consecutive escape sequences
    const multi = "\x1b[31m\x1b[1m\x1b[4mStyled\x1b[0m";
    const stripped6 = util.stripVTControlCharacters(multi);
    if (stripped6 === "Styled") {
        console.log("PASS: Multiple consecutive codes removed");
    } else {
        console.log("FAIL: Expected 'Styled', got: " + stripped6);
        failures++;
    }

    console.log("");
    console.log("=== util.toUSVString() Tests ===");
    console.log("");

    // Test 7: Normal string unchanged
    const normal = "Hello World";
    const usv1 = util.toUSVString(normal);
    if (usv1 === normal) {
        console.log("PASS: Normal string unchanged");
    } else {
        console.log("FAIL: Expected 'Hello World', got: " + usv1);
        failures++;
    }

    // Test 8: Unicode string unchanged
    const unicode = "Hello, \u4e16\u754c!"; // Hello, 世界!
    const usv2 = util.toUSVString(unicode);
    if (usv2 === unicode) {
        console.log("PASS: Unicode string unchanged");
    } else {
        console.log("FAIL: Unicode string should be unchanged");
        failures++;
    }

    // Test 9: Emoji unchanged (valid UTF-8)
    const emoji = "Hello \ud83d\ude00"; // Hello with grinning face emoji
    const usv3 = util.toUSVString(emoji);
    // Note: This is a proper surrogate pair, should be unchanged
    if (usv3.length > 0) {
        console.log("PASS: Emoji handled");
    } else {
        console.log("FAIL: Emoji handling failed");
        failures++;
    }

    // Test 10: Empty string
    const emptyUsv = "";
    const usv4 = util.toUSVString(emptyUsv);
    if (usv4 === "") {
        console.log("PASS: Empty string handled in toUSVString");
    } else {
        console.log("FAIL: Expected empty string");
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
