// Test: semver - semantic versioning parsing and comparison
// Exercises: regex parsing, class instances, string split/join, complex conditionals

import * as semver from 'semver';

function user_main(): number {
    let failures = 0;

    // Test 1: valid() returns cleaned version string
    const v1 = semver.valid("1.2.3");
    if (v1 === "1.2.3") {
        console.log("PASS: valid('1.2.3') === '1.2.3'");
    } else {
        console.log("FAIL: valid('1.2.3') expected '1.2.3', got '" + String(v1) + "'");
        failures++;
    }

    // Test 2: valid() returns null for invalid
    const v2 = semver.valid("invalid");
    if (v2 === null) {
        console.log("PASS: valid('invalid') === null");
    } else {
        console.log("FAIL: valid('invalid') expected null, got '" + String(v2) + "'");
        failures++;
    }

    // Test 3: gt() - greater than
    if (semver.gt("2.0.0", "1.0.0") === true) {
        console.log("PASS: gt('2.0.0', '1.0.0') === true");
    } else {
        console.log("FAIL: gt('2.0.0', '1.0.0') expected true");
        failures++;
    }

    // Test 4: lt() - less than
    if (semver.lt("1.0.0", "2.0.0") === true) {
        console.log("PASS: lt('1.0.0', '2.0.0') === true");
    } else {
        console.log("FAIL: lt('1.0.0', '2.0.0') expected true");
        failures++;
    }

    // Test 5: gte() - greater than or equal
    if (semver.gte("1.0.0", "1.0.0") === true) {
        console.log("PASS: gte('1.0.0', '1.0.0') === true");
    } else {
        console.log("FAIL: gte('1.0.0', '1.0.0') expected true");
        failures++;
    }

    // Test 6: satisfies() - version in range
    if (semver.satisfies("1.2.3", "^1.0.0") === true) {
        console.log("PASS: satisfies('1.2.3', '^1.0.0') === true");
    } else {
        console.log("FAIL: satisfies('1.2.3', '^1.0.0') expected true");
        failures++;
    }

    // Test 7: satisfies() - version out of range
    if (semver.satisfies("2.0.0", "^1.0.0") === false) {
        console.log("PASS: satisfies('2.0.0', '^1.0.0') === false");
    } else {
        console.log("FAIL: satisfies('2.0.0', '^1.0.0') expected false");
        failures++;
    }

    // Test 8: coerce() - loose parsing
    const coerced = semver.coerce("v1.2");
    if (coerced !== null && coerced.version === "1.2.0") {
        console.log("PASS: coerce('v1.2').version === '1.2.0'");
    } else {
        const cv = coerced !== null ? coerced.version : "null";
        console.log("FAIL: coerce('v1.2') expected version '1.2.0', got '" + cv + "'");
        failures++;
    }

    // Test 9: clean() - trim whitespace and v prefix
    const cleaned = semver.clean("  =v1.2.3  ");
    if (cleaned === "1.2.3") {
        console.log("PASS: clean('  =v1.2.3  ') === '1.2.3'");
    } else {
        console.log("FAIL: clean('  =v1.2.3  ') expected '1.2.3', got '" + String(cleaned) + "'");
        failures++;
    }

    // Test 10: inc() - increment patch
    const patched = semver.inc("1.2.3", "patch");
    if (patched === "1.2.4") {
        console.log("PASS: inc('1.2.3', 'patch') === '1.2.4'");
    } else {
        console.log("FAIL: inc('1.2.3', 'patch') expected '1.2.4', got '" + String(patched) + "'");
        failures++;
    }

    // Test 11: inc() - increment minor
    const minored = semver.inc("1.2.3", "minor");
    if (minored === "1.3.0") {
        console.log("PASS: inc('1.2.3', 'minor') === '1.3.0'");
    } else {
        console.log("FAIL: inc('1.2.3', 'minor') expected '1.3.0', got '" + String(minored) + "'");
        failures++;
    }

    // Test 12: inc() - increment major
    const majored = semver.inc("1.2.3", "major");
    if (majored === "2.0.0") {
        console.log("PASS: inc('1.2.3', 'major') === '2.0.0'");
    } else {
        console.log("FAIL: inc('1.2.3', 'major') expected '2.0.0', got '" + String(majored) + "'");
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All semver tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    process.exit(failures);
    return failures;
}
