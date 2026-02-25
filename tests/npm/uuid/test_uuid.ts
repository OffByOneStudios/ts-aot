// Test: uuid - RFC4122 UUID generation
// Exercises: crypto.randomBytes, Uint8Array, bitwise ops, hex formatting

import * as uuid from 'uuid';

function user_main(): number {
    let failures = 0;

    // Test 1: v4() returns string of length 36
    const id1 = uuid.v4();
    if (id1.length === 36) {
        console.log("PASS: v4() returns string of length 36");
    } else {
        console.log("FAIL: v4() expected length 36, got " + String(id1.length));
        failures++;
    }

    // Test 2: v4() matches UUID v4 format (simplified check)
    // Format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx where y is [89ab]
    const parts = id1.split("-");
    const formatOk = parts.length === 5 &&
        parts[0].length === 8 &&
        parts[1].length === 4 &&
        parts[2].length === 4 &&
        parts[3].length === 4 &&
        parts[4].length === 12 &&
        parts[2][0] === "4";
    if (formatOk) {
        console.log("PASS: v4() matches UUID v4 format");
    } else {
        console.log("FAIL: v4() format invalid: " + id1);
        failures++;
    }

    // Test 3: Two v4() calls produce different values
    const id2 = uuid.v4();
    if (id1 !== id2) {
        console.log("PASS: two v4() calls produce different values");
    } else {
        console.log("FAIL: two v4() calls produced same value: " + id1);
        failures++;
    }

    // Test 4: validate() returns true for valid UUID
    if (uuid.validate(id1) === true) {
        console.log("PASS: validate(v4()) === true");
    } else {
        console.log("FAIL: validate(v4()) expected true for " + id1);
        failures++;
    }

    // Test 5: validate() returns false for invalid string
    if (uuid.validate("not-a-uuid") === false) {
        console.log("PASS: validate('not-a-uuid') === false");
    } else {
        console.log("FAIL: validate('not-a-uuid') expected false");
        failures++;
    }

    // Test 6: version() returns 4 for v4 UUID
    const ver = uuid.version(id1);
    if (ver === 4) {
        console.log("PASS: version(v4()) === 4");
    } else {
        console.log("FAIL: version(v4()) expected 4, got " + String(ver));
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All uuid tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    process.exit(failures);
    return failures;
}
