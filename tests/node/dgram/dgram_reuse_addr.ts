// Test dgram.createSocket(options) with reuseAddr option
import * as dgram from 'dgram';

function user_main(): number {
    console.log("Testing dgram reuseAddr option...");

    // Create first socket with reuseAddr enabled
    const socket1 = dgram.createSocket({
        type: 'udp4',
        reuseAddr: true
    });

    let testPort = 0;

    socket1.on('listening', () => {
        const addr1 = socket1.address();
        testPort = addr1.port;
        console.log(`Socket 1 bound to ${addr1.address}:${addr1.port}`);

        // Create second socket with reuseAddr and try to bind to same port
        const socket2 = dgram.createSocket({
            type: 'udp4',
            reuseAddr: true
        });

        socket2.on('listening', () => {
            const addr2 = socket2.address();
            console.log(`Socket 2 bound to ${addr2.address}:${addr2.port}`);
            console.log("reuseAddr test passed - both sockets bound successfully");
            socket2.close();
        });

        socket2.on('error', (err: any) => {
            console.log(`Socket 2 error (expected on some systems): ${err}`);
            console.log("reuseAddr test completed");
        });

        socket2.on('close', () => {
            console.log("Socket 2 closed");
            socket1.close();
        });

        // Try to bind to the same port
        socket2.bind(testPort, '127.0.0.1');
    });

    socket1.on('close', () => {
        console.log("Socket 1 closed");
        console.log("Test complete");
    });

    socket1.on('error', (err: any) => {
        console.log(`Socket 1 error: ${err}`);
    });

    // Bind to a random port first
    socket1.bind(0, '127.0.0.1');

    return 0;
}
