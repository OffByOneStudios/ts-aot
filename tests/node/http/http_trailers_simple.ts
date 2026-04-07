// Simple test for HTTP trailers property access
import * as http from 'http';

function user_main(): number {
    const server = http.createServer((req, res) => {
        // Test rawTrailers property
        console.log("Testing req.rawTrailers...");
        const rawTrailers = req.rawTrailers;
        console.log("rawTrailers type:", typeof rawTrailers);

        // Test trailers property
        console.log("Testing req.trailers...");
        const trailers = req.trailers;
        console.log("trailers type:", typeof trailers);

        // Send response
        res.writeHead(200);
        res.end("OK");

        console.log("Response sent, closing server");
        server.close();
    });

    server.listen(9876, '127.0.0.1', () => {
        console.log("Server listening on port 9876");

        // Make a request
        const req = http.request({ host: '127.0.0.1', port: 9876, path: '/' }, (res) => {
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

    // Use a timeout to allow the test to complete and then exit
    setTimeout(() => {
        console.log("Test complete - exiting");
        process.exit(0);
    }, 500);

    return 0;
}
