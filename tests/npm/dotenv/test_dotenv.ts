// Test: dotenv - loads environment variables from .env file
// Exercises: node_modules resolution, fs.readFileSync, path, process.env mutation

import * as dotenv from 'dotenv';
import * as path from 'path';
import * as fs from 'fs';

function user_main(): number {
    let failures = 0;

    // Create a temporary .env file next to this script
    const envDir = path.dirname(process.argv[1]);
    const envPath = path.join(envDir, '.env.test');

    // Write test .env file
    fs.writeFileSync(envPath, 'TEST_KEY=hello_world\nTEST_NUM=42\nTEST_QUOTED="quoted value"\n');

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

    // Test 3: config() loads from file into process.env
    const result = dotenv.config({ path: envPath });
    if (process.env['TEST_KEY'] === 'hello_world') {
        console.log("PASS: dotenv.config() sets process.env.TEST_KEY");
    } else {
        console.log("FAIL: expected TEST_KEY='hello_world', got '" + process.env['TEST_KEY'] + "'");
        failures++;
    }

    // Test 4: Numeric value loaded as string
    if (process.env['TEST_NUM'] === '42') {
        console.log("PASS: dotenv.config() loads numeric as string");
    } else {
        console.log("FAIL: expected TEST_NUM='42', got '" + process.env['TEST_NUM'] + "'");
        failures++;
    }

    // Test 5: config() returns parsed object
    if (result.parsed && result.parsed['TEST_KEY'] === 'hello_world') {
        console.log("PASS: config() returns parsed values");
    } else {
        console.log("FAIL: config().parsed missing or wrong");
        failures++;
    }

    // Cleanup
    fs.unlinkSync(envPath);

    console.log("---");
    if (failures === 0) {
        console.log("All dotenv tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    process.exit(failures);
    return failures;
}
