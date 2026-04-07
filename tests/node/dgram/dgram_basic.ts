// Basic dgram (UDP) module test
import * as dgram from 'dgram';

function user_main(): number {
    console.log("Creating UDP socket...");

    const server = dgram.createSocket('udp4');

    server.on('message', (msg: any, rinfo: any) => {
        console.log(`Server received: ${msg}`);
        console.log(`From: ${rinfo.address}:${rinfo.port}`);
        server.close();
    });

    server.on('listening', () => {
        const addr = server.address();
        console.log(`Server listening on ${addr.address}:${addr.port}`);

        // Send a message to self
        const client = dgram.createSocket('udp4');
        const message = 'Hello UDP!';
        client.send(message, 0, message.length, addr.port, '127.0.0.1', () => {
            console.log("Message sent!");
            client.close();
        });
    });

    server.on('close', () => {
        console.log("Server closed");
    });

    server.on('error', (err: any) => {
        console.log(`Server error: ${err}`);
        server.close();
    });

    console.log("Binding to port 0...");
    server.bind(0, '127.0.0.1');

    return 0;
}
