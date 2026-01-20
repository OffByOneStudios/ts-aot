// Test for HTTP trailers support (req.rawTrailers, req.trailers, res.addTrailers)
import * as http from 'http';

function user_main(): number {
    let testsPassed = 0;

    // Test 1: IncomingMessage rawTrailers is available (empty by default)
    const server = http.createServer((req, res) => {
        // Test rawTrailers property
        const rawTrailers = req.rawTrailers;
        if (Array.isArray(rawTrailers)) {
            console.log("PASS: req.rawTrailers is an array");
            testsPassed++;
        } else {
            console.log("FAIL: req.rawTrailers is not an array");
        }

        // Test trailers property
        const trailers = req.trailers;
        if (trailers !== null && typeof trailers === 'object') {
            console.log("PASS: req.trailers is an object");
            testsPassed++;
        } else {
            console.log("FAIL: req.trailers is not an object");
        }

        // Test res.addTrailers
        res.writeHead(200, { 'Content-Type': 'text/plain' });
        res.write('Hello');

        // Add trailers before ending
        res.addTrailers({ 'X-Checksum': '12345', 'X-Custom': 'value' });

        res.end();
        console.log("PASS: res.addTrailers called successfully");
        testsPassed++;

        // Shut down server
        server.close();
    });

    server.on('close', () => {
        console.log(`Tests passed: ${testsPassed}/3`);
    });

    server.listen(0, () => {
        const addr = server.address();
        let port = 0;
        if (addr && typeof addr === 'object') {
            port = addr.port;
        }
        console.log(`Server listening on port ${port}`);

        // Make a request
        http.get({ host: '127.0.0.1', port: port, path: '/' }, (res) => {
            res.on('data', () => {});
            res.on('end', () => {
                console.log("Request completed");
            });
        });
    });

    return 0;
}
