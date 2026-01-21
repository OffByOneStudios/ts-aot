// HTTP/2 Client Tests

import * as http2 from 'http2';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: connect() returns a client session
    const session = http2.connect('http://localhost:8088');
    if (session !== undefined && session !== null) {
        console.log("PASS: connect returns client session");
        passed++;
    } else {
        console.log("FAIL: connect returns null/undefined");
        failed++;
    }

    // Test 2: Client session has type property (should be 1 for client)
    const sessionType = session.type;
    if (sessionType === 1) {
        console.log("PASS: session.type is 1 (client)");
        passed++;
    } else {
        console.log("FAIL: session.type is " + sessionType + " (expected 1)");
        failed++;
    }

    // Test 3: Session starts in connecting state
    const isConnecting = session.connecting;
    if (isConnecting === true) {
        console.log("PASS: session.connecting is true");
        passed++;
    } else {
        console.log("FAIL: session.connecting is " + isConnecting);
        failed++;
    }

    // Test 4: Session is not closed initially
    const isClosed = session.closed;
    if (isClosed === false) {
        console.log("PASS: session.closed is false");
        passed++;
    } else {
        console.log("FAIL: session.closed is " + isClosed);
        failed++;
    }

    // Test 5: Session is not destroyed initially
    const isDestroyed = session.destroyed;
    if (isDestroyed === false) {
        console.log("PASS: session.destroyed is false");
        passed++;
    } else {
        console.log("FAIL: session.destroyed is " + isDestroyed);
        failed++;
    }

    // Test 6: Session has localSettings property
    const localSettings = session.localSettings;
    if (localSettings !== undefined && localSettings !== null) {
        console.log("PASS: session.localSettings exists");
        passed++;
    } else {
        console.log("FAIL: session.localSettings is null/undefined");
        failed++;
    }

    // Test 7: Session destroy() works
    console.log("About to call destroy()...");
    session.destroy();
    console.log("Called destroy(), checking destroyed flag...");
    const destroyedAfter = session.destroyed;
    console.log("Got destroyed flag: " + destroyedAfter);
    if (destroyedAfter === true) {
        console.log("PASS: session.destroyed is true after destroy()");
        passed++;
    } else {
        console.log("FAIL: session.destroyed is " + destroyedAfter + " after destroy()");
        failed++;
    }

    // Print summary
    console.log("");
    console.log("HTTP/2 Client Tests: " + passed + " passed, " + failed + " failed");

    return 0;
}
