// Test writable.cork() and writable.uncork() methods
import * as fs from 'fs';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    const testFile = 'test_cork_uncork.txt';
    const ws = fs.createWriteStream(testFile);

    // Test 1: cork() method exists and is callable
    try {
        ws.cork();
        console.log('PASS: cork() method is callable');
        passed++;
    } catch (e) {
        console.log('FAIL: cork() method threw error');
        failed++;
    }

    // Test 2: uncork() method exists and is callable
    try {
        ws.uncork();
        console.log('PASS: uncork() method is callable');
        passed++;
    } catch (e) {
        console.log('FAIL: uncork() method threw error');
        failed++;
    }

    // Test 3: cork() returns the stream for chaining
    const result = ws.cork();
    if (result) {
        console.log('PASS: cork() returns stream for chaining');
        passed++;
    } else {
        console.log('FAIL: cork() should return stream for chaining');
        failed++;
    }
    ws.uncork();

    // Clean up
    ws.end();

    setTimeout(() => {
        try {
            fs.unlinkSync(testFile);
        } catch (e) {
            // Ignore cleanup errors
        }
    }, 100);

    console.log('Tests: ' + passed + ' passed, ' + failed + ' failed');
    return failed;
}
