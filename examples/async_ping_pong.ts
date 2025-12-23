/**
 * Async Ping-Pong Benchmark for ts-aot
 * Tests: Promises, microtasks, and the event loop.
 */

async function ping(n: number): Promise<number> {
    if (n === 0) return 0;
    return await pong(n - 1) + 1;
}

async function pong(n: number): Promise<number> {
    if (n === 0) return 0;
    return await ping(n - 1) + 1;
}

async function runBenchmark() {
    const iterations = 100000;
    
    console.log("Starting async benchmark...");
    const start = Date.now();
    
    const result = await ping(iterations);
    
    const end = Date.now();
    console.log("Async Ping-Pong Benchmark: " + (end - start) + "ms");
    console.log("Result: " + result);
}

runBenchmark();
