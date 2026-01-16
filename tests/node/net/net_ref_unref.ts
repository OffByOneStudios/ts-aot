// Test socket and server ref/unref methods
import * as net from 'net';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: server.ref() returns the server
    const server = net.createServer(() => {});
    const refResult = server.ref();
    if (refResult === server) {
        console.log('PASS: server.ref() returns the server');
        passed++;
    } else {
        console.log('FAIL: server.ref() should return the server');
        failed++;
    }

    // Test 2: server.unref() returns the server
    const unrefResult = server.unref();
    if (unrefResult === server) {
        console.log('PASS: server.unref() returns the server');
        passed++;
    } else {
        console.log('FAIL: server.unref() should return the server');
        failed++;
    }

    // Test 3: socket.ref() returns the socket
    const socket = new net.Socket();
    const socketRefResult = socket.ref();
    if (socketRefResult === socket) {
        console.log('PASS: socket.ref() returns the socket');
        passed++;
    } else {
        console.log('FAIL: socket.ref() should return the socket');
        failed++;
    }

    // Test 4: socket.unref() returns the socket
    const socketUnrefResult = socket.unref();
    if (socketUnrefResult === socket) {
        console.log('PASS: socket.unref() returns the socket');
        passed++;
    } else {
        console.log('FAIL: socket.unref() should return the socket');
        failed++;
    }

    // Test 5: server.getConnections() calls callback
    let connectionsCalled = false;
    server.getConnections((err: Error | null, count: number) => {
        connectionsCalled = true;
        if (err === null && count === 0) {
            console.log('PASS: server.getConnections() callback received (err=null, count=0)');
            passed++;
        } else {
            console.log('FAIL: server.getConnections() callback should receive (null, 0)');
            failed++;
        }
    });

    if (connectionsCalled) {
        console.log('PASS: server.getConnections() callback was called');
        passed++;
    } else {
        console.log('FAIL: server.getConnections() callback should be called');
        failed++;
    }

    // Cleanup
    server.close();

    console.log('Tests: ' + passed + ' passed, ' + failed + ' failed');
    return failed;
}
