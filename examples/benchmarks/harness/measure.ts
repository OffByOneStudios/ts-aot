/**
 * Timing and measurement utilities for benchmarks
 *
 * These utilities work with both ts-aot compiled binaries and Node.js
 */

/**
 * High-resolution timestamp in milliseconds
 */
export function now(): number {
    // Use performance.now() for sub-millisecond precision
    return performance.now();
}

/**
 * Get process memory usage (if available)
 */
export function getMemoryUsage(): { heapUsed: number; external: number; rss: number } {
    const usage = process.memoryUsage();
    return {
        heapUsed: usage.heapUsed,
        external: usage.external,
        rss: usage.rss
    };
}

/**
 * Format bytes to human-readable string
 */
export function formatBytes(bytes: number): string {
    if (bytes < 1024) return bytes + " B";
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(2) + " KB";
    if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(2) + " MB";
    return (bytes / (1024 * 1024 * 1024)).toFixed(2) + " GB";
}

/**
 * Format milliseconds to human-readable string
 */
export function formatTime(ms: number): string {
    if (ms < 1) return (ms * 1000).toFixed(2) + " us";
    if (ms < 1000) return ms.toFixed(2) + " ms";
    return (ms / 1000).toFixed(2) + " s";
}

/**
 * Calculate percentile from sorted array
 */
export function percentile(sorted: number[], p: number): number {
    if (sorted.length === 0) return 0;
    const index = Math.ceil((p / 100) * sorted.length) - 1;
    return sorted[Math.max(0, Math.min(index, sorted.length - 1))];
}

/**
 * Calculate statistics from an array of measurements
 */
export function calculateStats(measurements: number[]): {
    min: number;
    max: number;
    avg: number;
    p50: number;
    p95: number;
    p99: number;
    stddev: number;
} {
    if (measurements.length === 0) {
        return { min: 0, max: 0, avg: 0, p50: 0, p95: 0, p99: 0, stddev: 0 };
    }

    const sorted = measurements.slice().sort((a, b) => a - b);
    const sum = sorted.reduce((a, b) => a + b, 0);
    const avg = sum / sorted.length;

    // Calculate standard deviation
    const squaredDiffs = sorted.map(x => Math.pow(x - avg, 2));
    const variance = squaredDiffs.reduce((a, b) => a + b, 0) / sorted.length;
    const stddev = Math.sqrt(variance);

    return {
        min: sorted[0],
        max: sorted[sorted.length - 1],
        avg: avg,
        p50: percentile(sorted, 50),
        p95: percentile(sorted, 95),
        p99: percentile(sorted, 99),
        stddev: stddev
    };
}

/**
 * Simple timer class for measuring elapsed time
 */
export class Timer {
    private startTime: number = 0;
    private elapsed: number = 0;
    private running: boolean = false;

    start(): void {
        this.startTime = now();
        this.running = true;
    }

    stop(): number {
        if (this.running) {
            this.elapsed = now() - this.startTime;
            this.running = false;
        }
        return this.elapsed;
    }

    reset(): void {
        this.startTime = 0;
        this.elapsed = 0;
        this.running = false;
    }

    getElapsed(): number {
        if (this.running) {
            return now() - this.startTime;
        }
        return this.elapsed;
    }
}

/**
 * Measure the execution time of a function
 */
export function measureSync<T>(fn: () => T): { result: T; elapsed: number } {
    const start = now();
    const result = fn();
    const elapsed = now() - start;
    return { result, elapsed };
}

/**
 * Measure the execution time of an async function
 */
export async function measureAsync<T>(fn: () => Promise<T>): Promise<{ result: T; elapsed: number }> {
    const start = now();
    const result = await fn();
    const elapsed = now() - start;
    return { result, elapsed };
}
