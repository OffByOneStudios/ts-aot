// Simple dgram test WITH closures
import * as dgram from 'dgram';

function user_main(): number {
    console.log("Creating UDP socket...");

    const server = dgram.createSocket('udp4');

    server.on('listening', () => {
        console.log("Listening event triggered");

        // Create client
        console.log("Creating client socket...");
        const client = dgram.createSocket('udp4');
        console.log("Client socket created");

        // Close via setTimeout (closure captures client)
        setTimeout(() => {
            console.log("In setTimeout callback...");
            console.log("Calling client.close() from closure...");
            client.close();
            console.log("client.close() returned");
            server.close();
        }, 10);

        console.log("setTimeout registered");
    });

    server.on('close', () => {
        console.log("Server closed");
    });

    console.log("Binding to port 0...");
    server.bind(0, '127.0.0.1');

    return 0;
}
