// Test for HTTP ClientRequest properties (no network connection)
import * as http from 'http';

function user_main(): number {
    console.log("Testing HTTP ClientRequest properties...");
    let failed = 0;

    // Create a request with local options (no actual connection)
    const options = {
        hostname: '127.0.0.1',
        port: 9999,  // Non-existent port
        path: '/api/test',
        method: 'POST'
    };

    // Create request but don't let it connect by destroying immediately
    const req = http.request(options, (res: http.IncomingMessage) => {
        // Response callback - never called
    });

    // Suppress error events and destroy request immediately
    req.on('error', (err: any) => {
        // Expected - connection refused
    });

    // Test path property (before connection attempt)
    const path = req.path;
    if (path === "/api/test") {
        console.log("PASS: req.path is /api/test");
    } else {
        console.log("FAIL: req.path is /api/test, got: " + path);
        failed++;
    }

    // Test method property
    const method = req.method;
    if (method === "POST") {
        console.log("PASS: req.method is POST");
    } else {
        console.log("FAIL: req.method is POST, got: " + method);
        failed++;
    }

    // Test host property
    const host = req.host;
    if (host === "127.0.0.1") {
        console.log("PASS: req.host is 127.0.0.1");
    } else {
        console.log("FAIL: req.host is 127.0.0.1, got: " + host);
        failed++;
    }

    // Test protocol property
    const protocol = req.protocol;
    if (protocol === "http:") {
        console.log("PASS: req.protocol is http:");
    } else {
        console.log("FAIL: req.protocol is http:, got: " + protocol);
        failed++;
    }

    console.log("");
    console.log("Results: " + (4 - failed) + " passed, " + failed + " failed");

    return failed > 0 ? 1 : 0;
}
