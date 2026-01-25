/**
 * HTTP Hello World Server Benchmark
 *
 * A minimal HTTP server for measuring request throughput.
 * Use external tools like autocannon, wrk, or hey to benchmark.
 *
 * Benchmark commands:
 *   autocannon -c 100 -d 10 http://localhost:3000
 *   wrk -t4 -c100 -d10s http://localhost:3000
 *   hey -n 10000 -c 100 http://localhost:3000
 *
 * The server responds with "Hello, World!" to all requests.
 */

import * as http from 'http';

const PORT = parseInt(process.env.PORT || '3000', 10);
const HOST = process.env.HOST || '127.0.0.1';

// Response data (pre-computed for efficiency)
const RESPONSE_BODY = 'Hello, World!';
const RESPONSE_HEADERS = {
    'Content-Type': 'text/plain',
    'Content-Length': RESPONSE_BODY.length.toString(),
    'Connection': 'keep-alive'
};

// Statistics
let requestCount = 0;
let startTime = Date.now();

/**
 * Request handler - as minimal as possible
 */
function handleRequest(req: http.IncomingMessage, res: http.ServerResponse): void {
    requestCount++;

    res.writeHead(200, RESPONSE_HEADERS);
    res.end(RESPONSE_BODY);
}

/**
 * Print statistics periodically
 */
function printStats(): void {
    const elapsed = (Date.now() - startTime) / 1000;
    const rps = requestCount / elapsed;

    console.log(`Requests: ${requestCount}, Elapsed: ${elapsed.toFixed(1)}s, RPS: ${rps.toFixed(0)}`);
}

function user_main(): number {
    console.log('HTTP Hello World Server');
    console.log('=======================');
    console.log('');

    const server = http.createServer(handleRequest);

    server.on('error', (err: Error) => {
        console.error('Server error:', err.message);
        process.exit(1);
    });

    server.listen(PORT, HOST, () => {
        console.log(`Server listening on http://${HOST}:${PORT}`);
        console.log('');
        console.log('Benchmark with:');
        console.log(`  autocannon -c 100 -d 10 http://${HOST}:${PORT}`);
        console.log(`  wrk -t4 -c100 -d10s http://${HOST}:${PORT}`);
        console.log(`  hey -n 10000 -c 100 http://${HOST}:${PORT}`);
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

    // Keep the process running
    return 0;
}
