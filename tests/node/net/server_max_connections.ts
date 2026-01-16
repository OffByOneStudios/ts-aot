// Test server.maxConnections property
import * as net from 'net';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: default maxConnections is -1 (unlimited)
    const server = net.createServer(() => {});
    if (server.maxConnections === -1) {
        console.log('PASS: default maxConnections is -1');
        passed++;
    } else {
        console.log('FAIL: default maxConnections should be -1, got ' + server.maxConnections);
        failed++;
    }

    // Test 2: set maxConnections to a positive value
    server.maxConnections = 10;
    if (server.maxConnections === 10) {
        console.log('PASS: maxConnections can be set to 10');
        passed++;
    } else {
        console.log('FAIL: maxConnections should be 10, got ' + server.maxConnections);
        failed++;
    }

    // Test 3: set maxConnections to 0
    server.maxConnections = 0;
    if (server.maxConnections === 0) {
        console.log('PASS: maxConnections can be set to 0');
        passed++;
    } else {
        console.log('FAIL: maxConnections should be 0, got ' + server.maxConnections);
        failed++;
    }

    // Cleanup
    server.close();

    console.log('Tests: ' + passed + ' passed, ' + failed + ' failed');
    return failed;
}
