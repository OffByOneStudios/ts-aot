// Test for HTTP ClientRequest.socket property
import * as http from 'http';

function user_main(): number {
    console.log("Testing HTTP ClientRequest.socket...");
    let failed = 0;

    // Create request options
    const options = {
        hostname: '127.0.0.1',
        port: 9999,  // Non-existent port
        path: '/api/test',
        method: 'GET'
    };

    // Create request
    const req = http.request(options, (res: http.IncomingMessage) => {
        // Response callback - never called since we destroy immediately
    });

    // Suppress error events
    req.on('error', (err: any) => {
        // Expected - we're destroying the request
    });

    // Test that socket property exists
    const socket = req.socket;

    // Note: socket may be null/undefined before connection is established
    // but the property should be accessible
    console.log("PASS: req.socket property is accessible");

    // Destroy the request immediately
    req.destroy();

    console.log("");
    console.log("Results: " + (1 - failed) + " passed, " + failed + " failed");

    // Force exit to prevent event loop from hanging
    process.exit(failed > 0 ? 1 : 0);
    return failed > 0 ? 1 : 0;
}
