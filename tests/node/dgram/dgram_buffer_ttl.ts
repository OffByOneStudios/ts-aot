// Test dgram buffer size and TTL configuration
import * as dgram from 'dgram';

function user_main(): number {
    console.log("Testing UDP buffer size and TTL...");

    const socket = dgram.createSocket('udp4');

    socket.on('listening', () => {
        const addr = socket.address();
        console.log(`Socket bound to ${addr.address}:${addr.port}`);

        // Get initial buffer sizes
        const initialRecvSize = socket.getRecvBufferSize();
        const initialSendSize = socket.getSendBufferSize();
        console.log(`Initial recv buffer size: ${initialRecvSize}`);
        console.log(`Initial send buffer size: ${initialSendSize}`);

        // Set new buffer sizes
        socket.setRecvBufferSize(65536);
        socket.setSendBufferSize(65536);
        console.log("Buffer sizes set to 65536");

        // Get new buffer sizes
        const newRecvSize = socket.getRecvBufferSize();
        const newSendSize = socket.getSendBufferSize();
        console.log(`New recv buffer size: ${newRecvSize}`);
        console.log(`New send buffer size: ${newSendSize}`);

        // Set TTL
        socket.setTTL(64);
        console.log("TTL set to 64");

        console.log("Buffer size and TTL tests passed");
        socket.close();
    });

    socket.on('close', () => {
        console.log("Socket closed");
    });

    socket.bind(0);

    return 0;
}
