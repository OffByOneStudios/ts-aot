// HTTP/2 Server Tests

import * as http2 from 'http2';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: createServer returns a server object
    const server = http2.createServer();
    if (server !== undefined && server !== null) {
        console.log("PASS: createServer returns server object");
        passed++;
    } else {
        console.log("FAIL: createServer returns null/undefined");
        failed++;
    }

    // Test 2: createSecureServer returns a server object
    const secureServer = http2.createSecureServer({});
    if (secureServer !== undefined && secureServer !== null) {
        console.log("PASS: createSecureServer returns server object");
        passed++;
    } else {
        console.log("FAIL: createSecureServer returns null/undefined");
        failed++;
    }

    // Test 3: server.listen() starts listening
    let listenCalled = false;
    server.on('listening', () => {
        listenCalled = true;
    });
    server.listen(8088, () => {
        console.log("PASS: server.listen callback called");
        passed++;

        // Test 4: server.address() returns address info
        const addr = server.address();
        if (addr !== null && addr !== undefined) {
            console.log("PASS: server.address returns info");
            passed++;
        } else {
            console.log("FAIL: server.address returns null");
            failed++;
        }

        // Test 5: Close the server
        server.close(() => {
            console.log("PASS: server.close callback called");
            passed++;

            // Print summary
            console.log("");
            console.log("HTTP/2 Server Tests: " + passed + " passed, " + failed + " failed");
        });
    });

    return 0;
}
