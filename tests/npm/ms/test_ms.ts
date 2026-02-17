// Test: ms - tiny millisecond conversion utility
// Exercises: node_modules resolution, pure computation (no Node.js APIs)
// This is the simplest possible npm package test - zero I/O, just math + strings

import ms from 'ms';

function user_main(): number {
    let failures = 0;

    // Test 1: String to milliseconds
    if (ms('2 days') === 172800000) {
        console.log("PASS: ms('2 days') === 172800000");
    } else {
        console.log("FAIL: ms('2 days') expected 172800000, got " + ms('2 days'));
        failures++;
    }

    // Test 2: Hours
    if (ms('1h') === 3600000) {
        console.log("PASS: ms('1h') === 3600000");
    } else {
        console.log("FAIL: ms('1h') expected 3600000, got " + ms('1h'));
        failures++;
    }

    // Test 3: Minutes
    if (ms('5m') === 300000) {
        console.log("PASS: ms('5m') === 300000");
    } else {
        console.log("FAIL: ms('5m') expected 300000, got " + ms('5m'));
        failures++;
    }

    // Test 4: Seconds
    if (ms('10s') === 10000) {
        console.log("PASS: ms('10s') === 10000");
    } else {
        console.log("FAIL: ms('10s') expected 10000, got " + ms('10s'));
        failures++;
    }

    // Test 5: Milliseconds passthrough
    if (ms('100') === 100) {
        console.log("PASS: ms('100') === 100");
    } else {
        console.log("FAIL: ms('100') expected 100, got " + ms('100'));
        failures++;
    }

    // Test 6: Number to string (long format)
    const longStr = ms(60000, { long: true });
    if (longStr === '1 minute') {
        console.log("PASS: ms(60000, {long:true}) === '1 minute'");
    } else {
        console.log("FAIL: ms(60000, {long:true}) expected '1 minute', got '" + longStr + "'");
        failures++;
    }

    // Test 7: Number to short string
    const shortStr = ms(3600000);
    if (shortStr === '1h') {
        console.log("PASS: ms(3600000) === '1h'");
    } else {
        console.log("FAIL: ms(3600000) expected '1h', got '" + shortStr + "'");
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All ms tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    process.exit(failures);
    return failures;
}
