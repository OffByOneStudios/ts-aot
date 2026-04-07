// Test http.Server timeout configuration
import * as http from 'http';

function user_main(): number {
    console.log("Testing HTTP Server timeout configuration...");

    const server = http.createServer((req, res) => {
        res.writeHead(200);
        res.end('OK');
    });

    // Test default timeout values
    console.log("Default timeout: " + server.timeout);
    console.log("Default keepAliveTimeout: " + server.keepAliveTimeout);
    console.log("Default headersTimeout: " + server.headersTimeout);
    console.log("Default requestTimeout: " + server.requestTimeout);
    console.log("Default maxHeadersCount: " + server.maxHeadersCount);

    // Test setting timeout with callback
    server.setTimeout(30000, () => {
        console.log("Server timeout callback called");
    });
    console.log("After setTimeout: " + server.timeout);

    // Start server to verify it works
    server.listen(0, '127.0.0.1', () => {
        const addr = server.address();
        console.log("Server listening on port " + addr.port);

        // Test making a request with timeout
        const options = {
            hostname: '127.0.0.1',
            port: addr.port,
            path: '/',
            method: 'GET'
        };

        const req = http.request(options, (res) => {
            console.log("Response status: " + res.statusCode);

            // Test request timeout methods
            req.setTimeout(5000, () => {
                console.log("Request timeout callback");
            });

            res.on('data', () => {});
            res.on('end', () => {
                console.log("Request completed");
                server.close(() => {
                    console.log("Server closed");
                    console.log("Test passed");
                });
            });
        });

        req.on('error', (e) => {
            console.log("Request error: " + e.message);
        });

        req.end();
    });

    // Exit after tests complete
    setTimeout(() => process.exit(0), 1000);

    return 0;
}
