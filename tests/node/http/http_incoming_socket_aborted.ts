// Test for req.socket and req.aborted properties on IncomingMessage
import * as http from 'http';

function user_main(): number {
    console.log("=== IncomingMessage socket and aborted Tests ===");

    const server = http.createServer((req, res) => {
        // Test 1: req.aborted is boolean and false
        console.log("\nTest 1: req.aborted...");
        const aborted = req.aborted;
        console.log("req.aborted type: " + typeof aborted);
        console.log("req.aborted value: " + aborted);
        if (aborted === false) {
            console.log("PASS: req.aborted is false");
        } else {
            console.log("FAIL: req.aborted should be false");
        }

        // Test 2: req.socket exists
        console.log("\nTest 2: req.socket...");
        const socket = req.socket;
        console.log("req.socket type: " + typeof socket);
        console.log("req.socket value check: " + (socket === null ? "null" : socket === undefined ? "undefined" : "exists"));

        if (socket !== null && socket !== undefined) {
            console.log("PASS: req.socket exists");
            console.log("socket.remoteAddress: " + socket.remoteAddress);
        } else {
            console.log("FAIL: req.socket is null or undefined");
        }

        // Send response
        res.writeHead(200);
        res.end("OK");

        console.log("\nClosing server...");
        server.close();
    });

    server.listen(9878, '127.0.0.1', () => {
        console.log("Server listening on port 9878");

        // Make a request
        const req = http.request({ host: '127.0.0.1', port: 9878, path: '/' }, (res) => {
            console.log("Got response");
            res.on('data', () => {});
            res.on('end', () => {
                console.log("Response ended");
            });
        });

        req.on('error', (e) => {
            console.log("Request error");
        });

        req.end();
    });

    // Exit after tests complete
    setTimeout(() => process.exit(0), 500);

    return 0;
}
