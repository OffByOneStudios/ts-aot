/**
 * TCP Echo Server Benchmark
 *
 * A simple TCP server that echoes back all received data.
 * Tests raw socket I/O throughput without HTTP overhead.
 *
 * Test with:
 *   echo "Hello" | nc localhost 3001
 *   # Or for sustained load:
 *   for i in {1..1000}; do echo "test $i" | nc localhost 3001 & done
 *
 * For proper benchmarking, use a tool like:
 *   tcpkali -c 100 -m "Hello World" -T 10s 127.0.0.1:3001
 */

import * as net from 'net';

const PORT = parseInt(process.env.PORT || '3001', 10);
const HOST = process.env.HOST || '127.0.0.1';

// Statistics
let connectionCount = 0;
let totalBytesReceived = 0;
let totalBytesSent = 0;
let startTime = Date.now();

/**
 * Handle a client connection
 */
function handleConnection(socket: net.Socket): void {
    connectionCount++;

    socket.on('data', (data: Buffer) => {
        totalBytesReceived += data.length;

        // Echo the data back
        socket.write(data);
        totalBytesSent += data.length;
    });

    socket.on('error', (err: Error) => {
        // Ignore connection reset errors (normal during benchmarking)
        if (err.message.indexOf('ECONNRESET') === -1) {
            console.error('Socket error:', err.message);
        }
    });

    socket.on('close', () => {
        // Connection closed
    });
}

/**
 * Format bytes to human-readable
 */
function formatBytes(bytes: number): string {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(2) + ' KB';
    if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(2) + ' MB';
    return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
}

/**
 * Print statistics periodically
 */
function printStats(): void {
    const elapsed = (Date.now() - startTime) / 1000;
    const throughputIn = totalBytesReceived / elapsed;
    const throughputOut = totalBytesSent / elapsed;

    console.log(`Connections: ${connectionCount}`);
    console.log(`  Received: ${formatBytes(totalBytesReceived)} (${formatBytes(throughputIn)}/s)`);
    console.log(`  Sent:     ${formatBytes(totalBytesSent)} (${formatBytes(throughputOut)}/s)`);
    console.log(`  Elapsed:  ${elapsed.toFixed(1)}s`);
}

function user_main(): number {
    console.log('TCP Echo Server Benchmark');
    console.log('=========================');
    console.log('');

    const server = net.createServer(handleConnection);

    server.on('error', (err: Error) => {
        console.error('Server error:', err.message);
        process.exit(1);
    });

    server.listen(PORT, HOST, () => {
        console.log(`Server listening on tcp://${HOST}:${PORT}`);
        console.log('');
        console.log('Test with:');
        console.log(`  echo "Hello" | nc ${HOST} ${PORT}`);
        console.log(`  tcpkali -c 100 -m "Hello World" -T 10s ${HOST}:${PORT}`);
        console.log('');
        console.log('Press Ctrl+C to stop.');
        console.log('');

        startTime = Date.now();

        // Print stats every 5 seconds
        setInterval(printStats, 5000);
    });

    // Handle graceful shutdown
    process.on('SIGINT', () => {
        console.log('');
        console.log('Shutting down...');
        printStats();
        server.close(() => {
            console.log('Server closed.');
            process.exit(0);
        });
    });

    return 0;
}
