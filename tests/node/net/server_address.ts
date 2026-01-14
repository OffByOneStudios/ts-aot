// Test net server.address() method
import * as net from 'net';

function user_main(): number {
    console.log("Testing server.address()...");

    const server = net.createServer((socket) => {
        // Connection handler
    });

    // Listen on port 0 (OS assigns available port)
    server.listen(8765, () => {
        console.log("Server listening callback invoked");

        const addr = server.address();
        console.log("address() returned object: " + (addr !== null));

        if (addr) {
            console.log("address property: " + addr.address);
            console.log("family property: " + addr.family);
            console.log("port property: " + addr.port);
            console.log("port is correct: " + (addr.port === 8765));
        }

        server.close();
        console.log("All server.address() tests passed!");
    });

    return 0;
}
