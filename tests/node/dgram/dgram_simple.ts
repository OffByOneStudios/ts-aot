// Simple dgram test - testing send functionality
import * as dgram from 'dgram';

function user_main(): number {
    console.log("Creating UDP socket...");

    const server = dgram.createSocket('udp4');

    server.on('listening', () => {
        console.log("Listening event triggered");
        const addr = server.address();
        console.log("Got address object");

        if (addr) {
            console.log("Address object is not null");
            const a = addr.address;
            console.log("Got addr.address");
            const p = addr.port;
            console.log("Got addr.port");

            // Now test send
            console.log("Creating client socket...");
            const client = dgram.createSocket('udp4');
            console.log("Client socket created");

            const message = "Hello";
            console.log("Message created");

            // Use hardcoded port first to isolate issue
            console.log("Calling send...");
            client.send(message, 0, 5, p, '127.0.0.1', () => {
                console.log("Send callback executed");
                console.log("Calling client.close()...");
                client.close();
                console.log("client.close() returned");
            });
            console.log("Send called");
        }

        // Give time for the message to be sent
        setTimeout(() => {
            console.log("Closing server after timeout");
            server.close();
        }, 100);
    });

    server.on('message', (msg: any, rinfo: any) => {
        console.log("Server received message");
    });

    server.on('close', () => {
        console.log("Server closed");
    });

    console.log("Binding to port 0...");
    server.bind(0, '127.0.0.1');

    return 0;
}
