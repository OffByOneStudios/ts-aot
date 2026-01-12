// Test RegExp unicode flag (u) - ES2015

function user_main(): number {
    // Unicode flag test - properly handles surrogate pairs
    const text = "𝌆";  // U+1D306 (surrogate pair)

    // Without u flag, matches two code units
    const noUnicode = /^.$/;
    console.log("Unicode flag test:");
    console.log(noUnicode.test(text));  // false (two code units)

    // With u flag, matches one code point
    const withUnicode = /^.$/u;
    console.log(withUnicode.test(text));  // true (one code point)

    // Unicode property escapes
    const letter = /\p{L}/u;
    console.log(letter.test("a"));  // true
    console.log(letter.test("1"));  // false

    return 0;
}
