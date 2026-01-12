// Test ES2024 String.prototype.isWellFormed() and toWellFormed()

function user_main(): number {
    console.log("Testing String.prototype.isWellFormed() and toWellFormed()...");

    // Test 1: Well-formed strings
    const normal = "Hello, world!";
    console.log("Normal string isWellFormed: " + normal.isWellFormed());  // true

    // Test 2: ASCII characters
    const ascii = "abc123";
    console.log("ASCII isWellFormed: " + ascii.isWellFormed());  // true

    // Test 3: Valid emoji (proper surrogate pair)
    const emoji = "Hello \uD83D\uDE00!";  // Grinning face emoji
    console.log("Valid emoji isWellFormed: " + emoji.isWellFormed());  // true

    // Test 4: toWellFormed on well-formed string returns same string
    const result1 = normal.toWellFormed();
    console.log("toWellFormed on normal: " + result1);

    // Test 5: Empty string
    const empty = "";
    console.log("Empty string isWellFormed: " + empty.isWellFormed());  // true

    // Test 6: Unicode characters (not surrogates)
    const unicode = "\u00E9\u00F1\u00FC";  // accented characters
    console.log("Unicode isWellFormed: " + unicode.isWellFormed());  // true

    console.log("All well-formed string tests passed!");
    return 0;
}
