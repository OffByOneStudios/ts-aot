// Simple dgram test without closures
import * as dgram from 'dgram';

// Global variable to avoid closure issues
let globalClient: any = null;

function user_main(): number {
    console.log("Creating UDP socket...");

    const server = dgram.createSocket('udp4');

    server.on('listening', () => {
        console.log("Listening event triggered");

        // Create client and test close immediately in same scope
        console.log("Creating client socket...");
        const client = dgram.createSocket('udp4');
        console.log("Client socket created");

        // Close immediately - no closure involved
        console.log("Calling client.close() immediately...");
        client.close();
        console.log("client.close() returned");

        // Now close server
        server.close();
    });

    server.on('close', () => {
        console.log("Server closed");
    });

    console.log("Binding to port 0...");
    server.bind(0);

    return 0;
}
