// HTTP/2 Request Tests

import * as http2 from 'http2';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: session.request() returns a stream
    const session = http2.connect('http://localhost:8088');

    // Handle expected connection error (no server running)
    session.on('error', (err: any) => {
        // Expected - no server is running, just ignore
    });

    const stream = session.request({
        ':method': 'GET',
        ':path': '/'
    });

    if (stream !== undefined && stream !== null) {
        console.log("PASS: request returns client stream");
        passed++;
    } else {
        console.log("FAIL: request returns null/undefined");
        failed++;
    }

    // Test 2: Stream has id property (should be > 0 for client streams)
    const streamId = stream.id;
    if (streamId > 0) {
        console.log("PASS: stream.id is " + streamId + " (positive)");
        passed++;
    } else {
        console.log("FAIL: stream.id is " + streamId + " (expected > 0)");
        failed++;
    }

    // Test 3: Stream is not closed initially
    const isClosed = stream.closed;
    if (isClosed === false) {
        console.log("PASS: stream.closed is false");
        passed++;
    } else {
        console.log("FAIL: stream.closed is " + isClosed);
        failed++;
    }

    // Test 4: Stream is not destroyed initially
    const isDestroyed = stream.destroyed;
    if (isDestroyed === false) {
        console.log("PASS: stream.destroyed is false");
        passed++;
    } else {
        console.log("FAIL: stream.destroyed is " + isDestroyed);
        failed++;
    }

    // Test 5: Stream is pending initially (hasn't been sent yet)
    const isPending = stream.pending;
    if (isPending === true) {
        console.log("PASS: stream.pending is true");
        passed++;
    } else {
        console.log("FAIL: stream.pending is " + isPending);
        failed++;
    }

    // Test 6: Stream has aborted property (should be false)
    const isAborted = stream.aborted;
    if (isAborted === false) {
        console.log("PASS: stream.aborted is false");
        passed++;
    } else {
        console.log("FAIL: stream.aborted is " + isAborted);
        failed++;
    }

    // Clean up
    session.destroy();

    // Print summary
    console.log("");
    console.log("HTTP/2 Request Tests: " + passed + " passed, " + failed + " failed");

    return 0;
}
