// Test: process.argv Node.js layout compatibility
// In Node.js: argv = [nodePath, scriptPath, ...userArgs]
// In AOT: argv = [exePath, exePath, ...userArgs] (mimics Node.js layout)

import * as assert from 'assert';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: argv has at least 2 elements (exe path + script path)
    try {
        assert.ok(process.argv.length >= 2, 'argv should have at least 2 elements');
        passed++;
    } catch (e) {
        console.log('FAIL: argv length >= 2: ' + e);
        failed++;
    }

    // Test 2: argv[0] is a string (exe path)
    try {
        assert.strictEqual(typeof process.argv[0], 'string');
        assert.ok(process.argv[0].length > 0, 'argv[0] should be non-empty');
        passed++;
    } catch (e) {
        console.log('FAIL: argv[0] is string: ' + e);
        failed++;
    }

    // Test 3: argv[1] is a string (script path, same as exe in AOT)
    try {
        assert.strictEqual(typeof process.argv[1], 'string');
        assert.ok(process.argv[1].length > 0, 'argv[1] should be non-empty');
        passed++;
    } catch (e) {
        console.log('FAIL: argv[1] is string: ' + e);
        failed++;
    }

    // Test 4: argv.slice(2) returns user arguments (empty when no args passed)
    try {
        const userArgs = process.argv.slice(2);
        // When run without extra args, userArgs should be empty
        // This is the key compatibility point - code using slice(2) should work
        assert.strictEqual(userArgs.length, 0);
        passed++;
    } catch (e) {
        console.log('FAIL: argv.slice(2): ' + e);
        failed++;
    }

    // Test 5: argv[0] and argv[1] should both be strings (compatible with Node.js pattern)
    try {
        // Common Node.js pattern: const [node, script, ...args] = process.argv;
        const node = process.argv[0];
        const script = process.argv[1];
        assert.strictEqual(typeof node, 'string');
        assert.strictEqual(typeof script, 'string');
        passed++;
    } catch (e) {
        console.log('FAIL: destructuring pattern: ' + e);
        failed++;
    }

    console.log(`process.argv layout: ${passed} passed, ${failed} failed out of ${passed + failed}`);
    return failed > 0 ? 1 : 0;
}
