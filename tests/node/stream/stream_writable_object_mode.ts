// Test writable.writableObjectMode property
import * as fs from 'fs';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: writableObjectMode on WriteStream (default false)
    const testFile = 'test_writable_object_mode.txt';
    const ws = fs.createWriteStream(testFile);

    const objectMode = ws.writableObjectMode;
    if (objectMode === false) {
        console.log('PASS: writableObjectMode is false by default');
        passed++;
    } else {
        console.log('FAIL: writableObjectMode should be false by default, got:', objectMode);
        failed++;
    }

    // Clean up
    ws.end();

    // Wait a bit for file operations to complete
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
