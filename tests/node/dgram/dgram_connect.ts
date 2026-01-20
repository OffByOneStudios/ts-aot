// Test dgram connect/disconnect functionality
import * as dgram from 'dgram';

function user_main(): number {
    console.log("Testing UDP connect/disconnect...");

    // Create a server to receive messages
    const server = dgram.createSocket('udp4');

    server.on('message', (msg: Buffer, rinfo: any) => {
        console.log(`Server received: ${msg.toString()}`);
        console.log(`From: ${rinfo.address}:${rinfo.port}`);
        server.close();
    });

    server.on('listening', () => {
        const serverAddr = server.address();
        console.log(`Server listening on ${serverAddr.address}:${serverAddr.port}`);

        // Create client and connect to server
        const client = dgram.createSocket('udp4');

        client.on('connect', () => {
            console.log("Client connected event received");
        });

        client.on('error', (err: any) => {
            console.log(`Client error: ${err}`);
        });

        // Connect to the server
        console.log("Connecting client to server...");
        client.connect(serverAddr.port, '127.0.0.1', () => {
            console.log("Client connect callback called");

            // Send without specifying port/address (uses connected remote)
            const message = 'Hello from connected client!';
            // Note: After connect, we can send without port/address
            // but our current implementation still requires them
            client.send(message, 0, message.length, serverAddr.port, '127.0.0.1', () => {
                console.log("Message sent!");

                // Disconnect
                client.disconnect();
                console.log("Client disconnected");
                client.close();
            });
        });
    });

    server.on('close', () => {
        console.log("Server closed");
    });

    server.bind(0);

    return 0;
}
