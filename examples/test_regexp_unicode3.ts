// Test RegExp unicode flag (u) - property escapes

function user_main(): number {
    console.log("Unicode property test:");

    // Unicode property - letter category
    const letter = /^\p{L}+$/u;
    console.log(letter.test("Hello"));  // true
    console.log(letter.test("123"));    // false

    // Unicode property - number
    const digit = /^\p{N}+$/u;
    console.log(digit.test("123"));    // true
    console.log(digit.test("abc"));    // false

    return 0;
}
