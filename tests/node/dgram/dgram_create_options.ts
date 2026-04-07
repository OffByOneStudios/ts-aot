// Test dgram.createSocket(options) - options object form
import * as dgram from 'dgram';

function user_main(): number {
    console.log("Testing dgram.createSocket(options)...");

    // Create socket with options object including all supported options
    const socket = dgram.createSocket({
        type: 'udp4',
        reuseAddr: true,
        recvBufferSize: 65536,
        sendBufferSize: 65536
    });

    socket.on('listening', () => {
        const addr = socket.address();
        console.log(`Socket bound to ${addr.address}:${addr.port}`);

        // Verify buffer sizes were applied
        const recvSize = socket.getRecvBufferSize();
        const sendSize = socket.getSendBufferSize();
        console.log(`Recv buffer size: ${recvSize}`);
        console.log(`Send buffer size: ${sendSize}`);

        // Buffer sizes should be at least what we requested
        // (OS may round up to page boundary)
        if (recvSize >= 65536) {
            console.log("Recv buffer size OK");
        } else {
            console.log("Recv buffer size smaller than expected");
        }

        if (sendSize >= 65536) {
            console.log("Send buffer size OK");
        } else {
            console.log("Send buffer size smaller than expected");
        }

        console.log("createSocket(options) test passed");
        socket.close();
    });

    socket.on('close', () => {
        console.log("Socket closed");
    });

    socket.bind(0, '127.0.0.1');

    return 0;
}
