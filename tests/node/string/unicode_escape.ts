// Test ES6 Unicode escapes \u{}

function user_main(): number {
    console.log("=== Unicode Escape Test ===");

    // Test 1: Basic 4-digit unicode escape (ES5)
    console.log("Test 1: Basic unicode escape");
    const euro = "\u20AC";
    console.log(euro);  // €

    // Test 2: Extended unicode escape (ES6) - for code points > 0xFFFF
    console.log("Test 2: Extended unicode escapes");
    const smiley = "\u{1F600}";  // Grinning face emoji
    console.log(smiley);

    const rocket = "\u{1F680}";  // Rocket emoji
    console.log(rocket);

    // Test 3: Lower values should also work
    console.log("Test 3: Low code points");
    const a = "\u{0041}";  // 'A'
    console.log(a);

    const newline = "\u{000A}";  // newline
    console.log("before" + newline + "after");

    // Test 4: Multiple in a string
    console.log("Test 4: Multiple escapes");
    const combined = "\u{1F600}\u{1F680}\u{2764}";  // smiley, rocket, heart
    console.log(combined);

    console.log("=== Tests Complete ===");
    return 0;
}
