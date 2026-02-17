// Test Date constructor variants and instance methods

function user_main(): number {
    let failures = 0;

    // Test 1: new Date() returns object with getTime() > 0
    const now = new Date();
    if (now.getTime() > 0) {
        console.log("PASS: new Date() has getTime() > 0");
    } else {
        console.log("FAIL: new Date() getTime() should be > 0, got " + now.getTime());
        failures++;
    }

    // Test 2: new Date(ms) roundtrip - getTime returns same value
    const ms = 1704067200000; // 2024-01-01T00:00:00.000Z
    const d = new Date(ms);
    if (d.getTime() === ms) {
        console.log("PASS: new Date(ms).getTime() === ms");
    } else {
        console.log("FAIL: getTime() expected " + ms + ", got " + d.getTime());
        failures++;
    }

    // Test 3: getUTCFullYear (use UTC to avoid timezone issues)
    if (d.getUTCFullYear() === 2024) {
        console.log("PASS: getUTCFullYear() === 2024");
    } else {
        console.log("FAIL: getUTCFullYear() expected 2024, got " + d.getUTCFullYear());
        failures++;
    }

    // Test 4: getUTCMonth (0-indexed, January = 0)
    if (d.getUTCMonth() === 0) {
        console.log("PASS: getUTCMonth() === 0");
    } else {
        console.log("FAIL: getUTCMonth() expected 0, got " + d.getUTCMonth());
        failures++;
    }

    // Test 5: getUTCDate
    if (d.getUTCDate() === 1) {
        console.log("PASS: getUTCDate() === 1");
    } else {
        console.log("FAIL: getUTCDate() expected 1, got " + d.getUTCDate());
        failures++;
    }

    // Test 6: getUTCHours on epoch-aligned date
    if (d.getUTCHours() === 0) {
        console.log("PASS: getUTCHours() === 0");
    } else {
        console.log("FAIL: getUTCHours() expected 0, got " + d.getUTCHours());
        failures++;
    }

    // Test 7: getUTCMinutes on epoch-aligned date
    if (d.getUTCMinutes() === 0) {
        console.log("PASS: getUTCMinutes() === 0");
    } else {
        console.log("FAIL: getUTCMinutes() expected 0, got " + d.getUTCMinutes());
        failures++;
    }

    // Test 8: valueOf equals getTime
    if (d.valueOf() === ms) {
        console.log("PASS: valueOf() === ms");
    } else {
        console.log("FAIL: valueOf() expected " + ms + ", got " + d.valueOf());
        failures++;
    }

    // Test 9: toString returns non-empty string
    const str = d.toString();
    if (str.length > 0) {
        console.log("PASS: toString() is non-empty");
    } else {
        console.log("FAIL: toString() returned empty string");
        failures++;
    }

    // Test 10: toDateString returns non-empty string
    const dateStr = d.toDateString();
    if (dateStr.length > 0) {
        console.log("PASS: toDateString() is non-empty");
    } else {
        console.log("FAIL: toDateString() returned empty string");
        failures++;
    }

    // Test 11: toISOString contains T and Z
    const iso = d.toISOString();
    if (iso.indexOf("T") > -1 && iso.indexOf("Z") > -1) {
        console.log("PASS: toISOString() contains T and Z");
    } else {
        console.log("FAIL: toISOString() expected ISO format, got '" + iso + "'");
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All date constructor tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    return failures;
}
