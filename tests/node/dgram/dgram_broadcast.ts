// Test dgram broadcast functionality
import * as dgram from 'dgram';

function user_main(): number {
    console.log("Testing UDP broadcast...");

    const socket = dgram.createSocket('udp4');
    console.log("Socket created");

    socket.on('listening', () => {
        console.log("Listening event received");
        const addr = socket.address();
        console.log(`Socket bound to ${addr.address}:${addr.port}`);

        // Enable broadcast
        socket.setBroadcast(true);
        console.log("Broadcast enabled");

        // Set multicast TTL
        socket.setMulticastTTL(128);
        console.log("Multicast TTL set to 128");

        // Set multicast loopback
        socket.setMulticastLoopback(true);
        console.log("Multicast loopback enabled");

        console.log("All broadcast/multicast settings applied successfully");
        socket.close();
    });

    socket.on('close', () => {
        console.log("Socket closed");
    });

    socket.on('error', (err: any) => {
        console.log(`Error: ${err}`);
        socket.close();
    });

    console.log("About to bind...");
    socket.bind(0, '127.0.0.1');
    console.log("Bind called");

    return 0;
}
