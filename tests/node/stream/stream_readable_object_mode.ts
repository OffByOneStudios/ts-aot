// Test readable.readableObjectMode property
import * as fs from 'fs';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Create a test file to read
    const testFile = 'test_readable_object_mode.txt';
    fs.writeFileSync(testFile, 'test content');

    // Test 1: readableObjectMode on ReadStream (default false)
    const rs = fs.createReadStream(testFile);

    const objectMode = rs.readableObjectMode;
    if (objectMode === false) {
        console.log('PASS: readableObjectMode is false by default');
        passed++;
    } else {
        console.log('FAIL: readableObjectMode should be false by default, got:', objectMode);
        failed++;
    }

    // Clean up
    rs.destroy();

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
