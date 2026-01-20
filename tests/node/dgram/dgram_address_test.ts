// Test dgram address() method
import * as dgram from 'dgram';

function user_main(): number {
    console.log("Creating UDP socket...");

    const server = dgram.createSocket('udp4');

    server.on('listening', () => {
        console.log("Listening event triggered");

        const addr = server.address();
        console.log("Got address object");

        // Check if addr is defined
        if (addr) {
            console.log("addr is defined");

            // Try accessing properties directly
            const a = addr.address;
            const p = addr.port;
            const f = addr.family;

            console.log("address: " + a);
            console.log("port: " + p);
            console.log("family: " + f);
        } else {
            console.log("addr is null/undefined");
        }

        server.close();
    });

    server.on('close', () => {
        console.log("Server closed");
    });

    console.log("Binding to port 0...");
    server.bind(0);

    return 0;
}
