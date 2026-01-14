// Test net socket configuration methods
import * as net from 'net';

function user_main(): number {
    console.log("Testing net socket configuration...");

    // Create a socket and test configuration methods
    const socket = new net.Socket();

    // Test setNoDelay (returns socket for chaining)
    const result1 = socket.setNoDelay(true);
    console.log("setNoDelay returned socket: " + (result1 === socket));

    // Test setKeepAlive (returns socket for chaining)
    const result2 = socket.setKeepAlive(true, 1000);
    console.log("setKeepAlive returned socket: " + (result2 === socket));

    // Test setTimeout (returns socket for chaining)
    const result3 = socket.setTimeout(5000);
    console.log("setTimeout returned socket: " + (result3 === socket));

    // Test chaining
    const chained = socket.setNoDelay(true).setKeepAlive(true, 500).setTimeout(3000);
    console.log("Chaining works: " + (chained === socket));

    console.log("All socket configuration tests passed!");
    return 0;
}
