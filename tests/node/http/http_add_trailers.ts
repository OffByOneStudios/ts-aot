// Test for res.addTrailers
import * as http from 'http';

function user_main(): number {
    const server = http.createServer((req, res) => {
        console.log("Request received");

        // Add trailers
        console.log("Adding trailers...");
        res.addTrailers({ 'X-Checksum': 'abc123' });
        console.log("Trailers added");

        // Send response
        res.writeHead(200);
        res.write("Hello");
        res.end(" World");

        console.log("Response sent, closing server");
        server.close();
    });

    server.listen(9877, '127.0.0.1', () => {
        console.log("Server listening on port 9877");

        // Make a request
        const req = http.request({ host: '127.0.0.1', port: 9877, path: '/' }, (res) => {
            console.log("Got response");
            res.on('data', (chunk) => {
                console.log("Data received");
            });
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
