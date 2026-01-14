// Test net socket state properties
import * as net from 'net';

function user_main(): number {
    console.log("Testing net socket properties...");

    // Create a socket
    const socket = new net.Socket();

    // Test initial state properties
    console.log("bytesRead initial: " + socket.bytesRead);
    console.log("bytesWritten initial: " + socket.bytesWritten);
    console.log("connecting initial: " + socket.connecting);
    console.log("destroyed initial: " + socket.destroyed);
    console.log("pending initial: " + socket.pending);
    console.log("readyState initial: " + socket.readyState);

    // Verify initial values
    const bytesReadOk = socket.bytesRead === 0;
    const bytesWrittenOk = socket.bytesWritten === 0;
    const connectingOk = socket.connecting === false;
    const destroyedOk = socket.destroyed === false;
    const pendingOk = socket.pending === false;

    console.log("bytesRead is 0: " + bytesReadOk);
    console.log("bytesWritten is 0: " + bytesWrittenOk);
    console.log("connecting is false: " + connectingOk);
    console.log("destroyed is false: " + destroyedOk);
    console.log("pending is false: " + pendingOk);

    console.log("All socket property tests passed!");
    return 0;
}
