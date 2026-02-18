// Test: dotenv - loads environment variables from .env file
// Exercises: node_modules resolution, untyped JS module compilation, regex parsing

import * as dotenv from 'dotenv';

function user_main(): number {
    let failures = 0;

    // Test 1: Parse .env content directly
    const content = 'FOO=bar\nBAZ=qux';
    const parsed = dotenv.parse(content);
    if (parsed['FOO'] === 'bar') {
        console.log("PASS: dotenv.parse() parses FOO=bar");
    } else {
        console.log("FAIL: expected FOO='bar', got '" + parsed['FOO'] + "'");
        failures++;
    }

    // Test 2: Parse second key
    if (parsed['BAZ'] === 'qux') {
        console.log("PASS: dotenv.parse() parses BAZ=qux");
    } else {
        console.log("FAIL: expected BAZ='qux', got '" + parsed['BAZ'] + "'");
        failures++;
    }

    // Test 3: Parse with comments
    const commentContent = 'KEY=value # this is a comment\nKEY2=value2';
    const parsed3 = dotenv.parse(commentContent);
    if (parsed3['KEY'] === 'value') {
        console.log("PASS: dotenv.parse() handles inline comments");
    } else {
        console.log("FAIL: expected KEY='value', got '" + parsed3['KEY'] + "'");
        failures++;
    }

    // Test 4: Parse multiple keys
    const multiContent = 'A=1\nB=2\nC=3';
    const parsed4 = dotenv.parse(multiContent);
    const keys = Object.keys(parsed4);
    if (keys.length === 3) {
        console.log("PASS: dotenv.parse() parses 3 keys correctly");
    } else {
        console.log("FAIL: expected 3 keys, got " + keys.length);
        failures++;
    }

    // Test 5: Parse export prefix
    const exportContent = 'export MY_VAR=exported';
    const parsed5 = dotenv.parse(exportContent);
    if (parsed5['MY_VAR'] === 'exported') {
        console.log("PASS: dotenv.parse() handles export prefix");
    } else {
        console.log("FAIL: expected MY_VAR='exported', got '" + parsed5['MY_VAR'] + "'");
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All dotenv tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    return failures;
}
