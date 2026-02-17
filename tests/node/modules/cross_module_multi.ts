// Test 3-file import chain: main -> models -> utils

import { User } from './cross_module_multi_models';
import { generateId, capitalize } from './cross_module_multi_utils';

function user_main(): number {
    let failures = 0;

    // Test 1: User class uses formatName from utils transitively
    const u = new User("Alice", "Smith");
    if (u.greet() === "Hello, Alice Smith") {
        console.log("PASS: User.greet() with transitive import");
    } else {
        console.log("FAIL: expected 'Hello, Alice Smith', got '" + u.greet() + "'");
        failures++;
    }

    // Test 2: Direct import from utils
    const id = generateId();
    if (id.length > 0) {
        console.log("PASS: generateId() returns non-empty string");
    } else {
        console.log("FAIL: generateId() returned empty string");
        failures++;
    }

    // Test 3: capitalize utility
    if (capitalize("hello") === "Hello") {
        console.log("PASS: capitalize works from direct import");
    } else {
        console.log("FAIL: capitalize expected 'Hello', got '" + capitalize("hello") + "'");
        failures++;
    }

    // Test 4: User name property accessible
    if (u.name === "Alice Smith") {
        console.log("PASS: User.name property accessible cross-module");
    } else {
        console.log("FAIL: expected 'Alice Smith', got '" + u.name + "'");
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All cross-module multi tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    return failures;
}
