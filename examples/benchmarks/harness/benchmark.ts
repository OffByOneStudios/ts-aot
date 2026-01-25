/**
 * Benchmark runner for ts-aot
 *
 * Provides a unified interface for running benchmarks with both
 * ts-aot compiled binaries and Node.js for comparison.
 */

import { now, calculateStats, formatTime, formatBytes, getMemoryUsage } from './measure';

/**
 * Options for benchmark configuration
 */
export interface BenchmarkOptions {
    /** Number of iterations to run (default: 100) */
    iterations?: number;
    /** Number of warmup iterations to discard (default: 10) */
    warmup?: number;
    /** Whether to collect memory statistics (default: false) */
    collectMemory?: boolean;
    /** Minimum time to run in milliseconds (default: 1000) */
    minTime?: number;
}

/**
 * Result of a benchmark run
 */
export interface BenchmarkResult {
    name: string;
    iterations: number;
    timing: {
        min: number;
        max: number;
        avg: number;
        p50: number;
        p95: number;
        p99: number;
        stddev: number;
    };
    memory?: {
        heapUsed: number;
        external: number;
        rss: number;
    };
    opsPerSecond: number;
}

/**
 * Default benchmark options
 */
const defaultOptions: Required<BenchmarkOptions> = {
    iterations: 100,
    warmup: 10,
    collectMemory: false,
    minTime: 1000
};

/**
 * Run a benchmark and collect statistics
 */
export function benchmark(
    name: string,
    fn: () => void,
    options?: BenchmarkOptions
): BenchmarkResult {
    const opts = { ...defaultOptions, ...options };

    // Warmup phase
    for (let i = 0; i < opts.warmup; i++) {
        fn();
    }

    // Measurement phase
    const measurements: number[] = [];
    let totalTime = 0;
    let actualIterations = 0;

    // Run at least opts.iterations times or until minTime is reached
    while (actualIterations < opts.iterations || totalTime < opts.minTime) {
        const start = now();
        fn();
        const elapsed = now() - start;
        measurements.push(elapsed);
        totalTime += elapsed;
        actualIterations++;

        // Safety limit to prevent infinite loops
        if (actualIterations >= opts.iterations * 10) break;
    }

    // Calculate statistics
    const stats = calculateStats(measurements);

    // Collect memory if requested
    let memory: BenchmarkResult['memory'] | undefined;
    if (opts.collectMemory) {
        const mem = getMemoryUsage();
        memory = {
            heapUsed: mem.heapUsed,
            external: mem.external,
            rss: mem.rss
        };
    }

    // Calculate operations per second
    const opsPerSecond = stats.avg > 0 ? 1000 / stats.avg : 0;

    return {
        name,
        iterations: actualIterations,
        timing: stats,
        memory,
        opsPerSecond
    };
}

/**
 * Run an async benchmark
 */
export async function benchmarkAsync(
    name: string,
    fn: () => Promise<void>,
    options?: BenchmarkOptions
): Promise<BenchmarkResult> {
    const opts = { ...defaultOptions, ...options };

    // Warmup phase
    for (let i = 0; i < opts.warmup; i++) {
        await fn();
    }

    // Measurement phase
    const measurements: number[] = [];
    let totalTime = 0;
    let actualIterations = 0;

    while (actualIterations < opts.iterations || totalTime < opts.minTime) {
        const start = now();
        await fn();
        const elapsed = now() - start;
        measurements.push(elapsed);
        totalTime += elapsed;
        actualIterations++;

        if (actualIterations >= opts.iterations * 10) break;
    }

    const stats = calculateStats(measurements);

    let memory: BenchmarkResult['memory'] | undefined;
    if (opts.collectMemory) {
        const mem = getMemoryUsage();
        memory = {
            heapUsed: mem.heapUsed,
            external: mem.external,
            rss: mem.rss
        };
    }

    const opsPerSecond = stats.avg > 0 ? 1000 / stats.avg : 0;

    return {
        name,
        iterations: actualIterations,
        timing: stats,
        memory,
        opsPerSecond
    };
}

/**
 * Format a benchmark result for console output
 */
export function formatResult(result: BenchmarkResult): string {
    const lines: string[] = [];

    lines.push(`Benchmark: ${result.name}`);
    lines.push(`  Iterations: ${result.iterations}`);
    lines.push(`  Timing:`);
    lines.push(`    Min:    ${formatTime(result.timing.min)}`);
    lines.push(`    Max:    ${formatTime(result.timing.max)}`);
    lines.push(`    Avg:    ${formatTime(result.timing.avg)}`);
    lines.push(`    P50:    ${formatTime(result.timing.p50)}`);
    lines.push(`    P95:    ${formatTime(result.timing.p95)}`);
    lines.push(`    P99:    ${formatTime(result.timing.p99)}`);
    lines.push(`    StdDev: ${formatTime(result.timing.stddev)}`);
    lines.push(`  Ops/sec:  ${result.opsPerSecond.toFixed(2)}`);

    if (result.memory) {
        lines.push(`  Memory:`);
        lines.push(`    Heap:     ${formatBytes(result.memory.heapUsed)}`);
        lines.push(`    External: ${formatBytes(result.memory.external)}`);
        lines.push(`    RSS:      ${formatBytes(result.memory.rss)}`);
    }

    return lines.join('\n');
}

/**
 * Format a benchmark result as JSON
 */
export function formatResultJSON(result: BenchmarkResult): string {
    return JSON.stringify(result, null, 2);
}

/**
 * Print benchmark result to console
 */
export function printResult(result: BenchmarkResult): void {
    console.log(formatResult(result));
}

/**
 * Benchmark suite for running multiple benchmarks
 */
export class BenchmarkSuite {
    private name: string;
    private benchmarks: Array<{ name: string; fn: () => void; options?: BenchmarkOptions }> = [];
    private results: BenchmarkResult[] = [];

    constructor(name: string) {
        this.name = name;
    }

    /**
     * Add a benchmark to the suite
     */
    add(name: string, fn: () => void, options?: BenchmarkOptions): this {
        this.benchmarks.push({ name, fn, options });
        return this;
    }

    /**
     * Run all benchmarks in the suite
     */
    run(): BenchmarkResult[] {
        console.log(`\n=== Benchmark Suite: ${this.name} ===\n`);

        this.results = [];

        for (const bench of this.benchmarks) {
            console.log(`Running: ${bench.name}...`);
            const result = benchmark(bench.name, bench.fn, bench.options);
            this.results.push(result);
            printResult(result);
            console.log('');
        }

        return this.results;
    }

    /**
     * Get the results of the last run
     */
    getResults(): BenchmarkResult[] {
        return this.results;
    }

    /**
     * Print a summary comparison table
     */
    printSummary(): void {
        console.log(`\n=== Summary: ${this.name} ===\n`);

        // Header
        const nameWidth = Math.max(...this.results.map(r => r.name.length), 10);
        const header = `| ${'Name'.padEnd(nameWidth)} | ${'Avg'.padStart(12)} | ${'P95'.padStart(12)} | ${'Ops/sec'.padStart(12)} |`;
        const separator = '|' + '-'.repeat(nameWidth + 2) + '|' + '-'.repeat(14) + '|' + '-'.repeat(14) + '|' + '-'.repeat(14) + '|';

        console.log(separator);
        console.log(header);
        console.log(separator);

        for (const result of this.results) {
            const row = `| ${result.name.padEnd(nameWidth)} | ${formatTime(result.timing.avg).padStart(12)} | ${formatTime(result.timing.p95).padStart(12)} | ${result.opsPerSecond.toFixed(2).padStart(12)} |`;
            console.log(row);
        }

        console.log(separator);
    }
}

// Export everything for use in benchmarks
export { now, formatTime, formatBytes, calculateStats, getMemoryUsage } from './measure';
