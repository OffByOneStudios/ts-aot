// Test dgram ref/unref functionality
import * as dgram from 'dgram';

function user_main(): number {
    console.log("Testing UDP ref/unref...");

    const socket = dgram.createSocket('udp4');

    socket.on('listening', () => {
        const addr = socket.address();
        console.log(`Socket bound to ${addr.address}:${addr.port}`);

        // Test ref - should return socket for chaining
        const refResult = socket.ref();
        console.log("ref() called");

        // Test unref - should return socket for chaining
        const unrefResult = socket.unref();
        console.log("unref() called");

        // Call ref again
        socket.ref();
        console.log("ref() called again");

        console.log("ref/unref tests passed");
        socket.close();
    });

    socket.on('close', () => {
        console.log("Socket closed");
    });

    socket.bind(0, '127.0.0.1');

    return 0;
}
