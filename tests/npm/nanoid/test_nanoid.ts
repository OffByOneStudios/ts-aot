// Test: nanoid - compact unique string ID generator
// Exercises: node_modules resolution, crypto.getRandomValues, URL-safe alphabet

import { nanoid, customAlphabet } from 'nanoid';

function user_main(): number {
    let failures = 0;

    // Test 1: Default nanoid produces 21-char string
    const id = nanoid();
    if (id.length === 21) {
        console.log("PASS: nanoid() returns 21-char string");
    } else {
        console.log("FAIL: expected length 21, got " + id.length);
        failures++;
    }

    // Test 2: ID contains only URL-safe characters
    const urlSafe = /^[A-Za-z0-9_-]+$/;
    if (urlSafe.test(id)) {
        console.log("PASS: nanoid() uses URL-safe alphabet");
    } else {
        console.log("FAIL: ID contains non-URL-safe chars: " + id);
        failures++;
    }

    // Test 3: Each call produces a different ID
    const id2 = nanoid();
    if (id !== id2) {
        console.log("PASS: nanoid() produces unique IDs");
    } else {
        console.log("FAIL: two calls returned same ID: " + id);
        failures++;
    }

    // Test 4: Custom length
    const id10 = nanoid(10);
    if (id10.length === 10) {
        console.log("PASS: nanoid(10) returns 10-char string");
    } else {
        console.log("FAIL: expected length 10, got " + id10.length);
        failures++;
    }

    // Test 5: customAlphabet - numeric only
    const numId = customAlphabet('0123456789', 8);
    const num = numId();
    if (num.length === 8) {
        console.log("PASS: customAlphabet returns correct length");
    } else {
        console.log("FAIL: expected length 8, got " + num.length);
        failures++;
    }

    // Test 6: customAlphabet uses only specified chars
    const numericOnly = /^[0-9]+$/;
    if (numericOnly.test(num)) {
        console.log("PASS: customAlphabet uses only specified chars");
    } else {
        console.log("FAIL: custom ID contains wrong chars: " + num);
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All nanoid tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    process.exit(failures);
    return failures;
}
