// Test: escape-string-regexp - escape RegExp special characters
// Exercises: String replacement, regex special character escaping
// Note: .d.ts removed from node_modules so compiler uses JS analysis path (like is-number)

import escapeStringRegexp from 'escape-string-regexp';

function user_main() {
    var failures = 0;

    // Test 1: Escape dot
    var dot = escapeStringRegexp('.');
    if (dot === '\\.') {
        console.log("PASS: escapeStringRegexp('.') === '\\.'");
    } else {
        console.log("FAIL: escapeStringRegexp('.') expected '\\.', got '" + dot + "'");
        failures++;
    }

    // Test 2: Escape complex string with multiple special chars
    var complex = escapeStringRegexp('How much $ for a *?');
    if (complex === 'How much \\$ for a \\*\\?') {
        console.log("PASS: escapeStringRegexp('How much $ for a *?')");
    } else {
        console.log("FAIL: expected 'How much \\$ for a \\*\\?', got '" + complex + "'");
        failures++;
    }

    // Test 3: Plain string unchanged
    var plain = escapeStringRegexp('hello');
    if (plain === 'hello') {
        console.log("PASS: escapeStringRegexp('hello') === 'hello'");
    } else {
        console.log("FAIL: escapeStringRegexp('hello') expected 'hello', got '" + plain + "'");
        failures++;
    }

    // Test 4: Escape brackets
    var brackets = escapeStringRegexp('[abc]');
    if (brackets === '\\[abc\\]') {
        console.log("PASS: escapeStringRegexp('[abc]') === '\\[abc\\]'");
    } else {
        console.log("FAIL: escapeStringRegexp('[abc]') expected '\\[abc\\]', got '" + brackets + "'");
        failures++;
    }

    // Test 5: Escape hyphen
    var hyphen = escapeStringRegexp('a-b');
    if (hyphen === 'a\\x2db') {
        console.log("PASS: escapeStringRegexp('a-b') === 'a\\x2db'");
    } else {
        console.log("FAIL: escapeStringRegexp('a-b') expected 'a\\x2db', got '" + hyphen + "'");
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All escape-string-regexp tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    return failures;
}
