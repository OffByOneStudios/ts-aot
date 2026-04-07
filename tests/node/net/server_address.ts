// Test net server.address() method
import * as net from 'net';

function user_main(): number {
    console.log("Testing server.address()...");

    const server = net.createServer((socket) => {
        // Connection handler
    });

    server.on('listening', () => {
        console.log("Server listening callback invoked");

        const addr = server.address();
        console.log("address() returned object: " + (addr !== null));

        if (addr) {
            console.log("address property: " + addr.address);
            console.log("family property: " + addr.family);
            console.log("port property: " + addr.port);
            console.log("port is number: " + (typeof addr.port === 'number'));
            console.log("port is positive: " + (addr.port > 0));
        }

        server.close();
        console.log("All server.address() tests passed!");
        process.exit(0);
    });

    // Listen on port 0 (OS assigns available port)
    server.listen(0, '127.0.0.1');

    // Timeout in case callback doesn't fire
    setTimeout(() => {
        console.log("FAIL: Timeout - listen callback never fired");
        process.exit(1);
    }, 5000);

    return 0;
}
