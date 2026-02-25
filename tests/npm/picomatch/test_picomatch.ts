// Test: picomatch - glob pattern matching
// Exercises: regex construction from strings, closures as return values, string manipulation

import picomatch from 'picomatch';

function user_main(): number {
    let failures = 0;

    // Test 1: Match *.js against "test.js"
    const isJs = picomatch("*.js");
    if (isJs("test.js") === true) {
        console.log("PASS: picomatch('*.js')('test.js') === true");
    } else {
        console.log("FAIL: picomatch('*.js')('test.js') expected true");
        failures++;
    }

    // Test 2: No match *.js against "test.ts"
    if (isJs("test.ts") === false) {
        console.log("PASS: picomatch('*.js')('test.ts') === false");
    } else {
        console.log("FAIL: picomatch('*.js')('test.ts') expected false");
        failures++;
    }

    // Test 3: Globstar **/*.js matches nested paths
    const isNestedJs = picomatch("**/*.js");
    if (isNestedJs("src/test.js") === true) {
        console.log("PASS: picomatch('**/*.js')('src/test.js') === true");
    } else {
        console.log("FAIL: picomatch('**/*.js')('src/test.js') expected true");
        failures++;
    }

    // Test 4: isMatch static method
    if (picomatch.isMatch("hello.txt", "*.txt") === true) {
        console.log("PASS: picomatch.isMatch('hello.txt', '*.txt') === true");
    } else {
        console.log("FAIL: picomatch.isMatch('hello.txt', '*.txt') expected true");
        failures++;
    }

    // Test 5: No match on different extension
    if (picomatch.isMatch("hello.md", "*.txt") === false) {
        console.log("PASS: picomatch.isMatch('hello.md', '*.txt') === false");
    } else {
        console.log("FAIL: picomatch.isMatch('hello.md', '*.txt') expected false");
        failures++;
    }

    // Test 6: Deeply nested globstar
    if (isNestedJs("a/b/c/deep.js") === true) {
        console.log("PASS: picomatch('**/*.js')('a/b/c/deep.js') === true");
    } else {
        console.log("FAIL: picomatch('**/*.js')('a/b/c/deep.js') expected true");
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All picomatch tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    process.exit(failures);
    return failures;
}
