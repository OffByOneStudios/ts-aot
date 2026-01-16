// Test readable.readableAborted, writable.writableAborted, readable.readableDidRead
import * as fs from 'fs';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: readableAborted on ReadStream (default false)
    const testFile = 'test_aborted.txt';
    fs.writeFileSync(testFile, 'test content');

    const rs = fs.createReadStream(testFile);
    if (rs.readableAborted === false) {
        console.log('PASS: readableAborted is false by default');
        passed++;
    } else {
        console.log('FAIL: readableAborted should be false by default');
        failed++;
    }

    // Test 2: readableDidRead on ReadStream (default false)
    if (rs.readableDidRead === false) {
        console.log('PASS: readableDidRead is false by default');
        passed++;
    } else {
        console.log('FAIL: readableDidRead should be false by default');
        failed++;
    }
    rs.destroy();

    // Test 3: writableAborted on WriteStream (default false)
    const ws = fs.createWriteStream('test_writable_aborted.txt');
    if (ws.writableAborted === false) {
        console.log('PASS: writableAborted is false by default');
        passed++;
    } else {
        console.log('FAIL: writableAborted should be false by default');
        failed++;
    }
    ws.end();

    // Cleanup
    setTimeout(() => {
        try {
            fs.unlinkSync(testFile);
            fs.unlinkSync('test_writable_aborted.txt');
        } catch (e) {
            // Ignore cleanup errors
        }
    }, 100);

    console.log('Tests: ' + passed + ' passed, ' + failed + ' failed');
    return failed;
}
