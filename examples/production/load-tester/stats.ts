/**
 * HTTP Load Tester — Statistics Collection
 *
 * Collects per-request latencies and computes statistical aggregates:
 * min, max, mean, median, percentiles, throughput, and error rates.
 */

export interface RequestResult {
    latencyMs: number;
    statusCode: number;
    success: boolean;
    error?: string;
    bytesSent: number;
    bytesReceived: number;
}

export interface PercentileResult {
    p50: number;
    p75: number;
    p90: number;
    p95: number;
    p99: number;
}

export interface StatsReport {
    totalRequests: number;
    successCount: number;
    errorCount: number;
    totalDurationMs: number;
    requestsPerSec: number;
    latencyMin: number;
    latencyMax: number;
    latencyMean: number;
    latencyMedian: number;
    percentiles: PercentileResult;
    statusCodes: Map<number, number>;
    errorMessages: Map<string, number>;
    totalBytesSent: number;
    totalBytesReceived: number;
}

/**
 * Calculate a specific percentile from a sorted array.
 */
function percentile(sorted: number[], p: number): number {
    if (sorted.length === 0) return 0;
    let idx = Math.ceil((p / 100) * sorted.length) - 1;
    if (idx < 0) idx = 0;
    if (idx > sorted.length - 1) idx = sorted.length - 1;
    return sorted[idx];
}

/**
 * Compute comprehensive statistics from a collection of request results.
 */
export function computeStats(results: RequestResult[], totalDurationMs: number): StatsReport {
    const totalRequests = results.length;
    let successCount = 0;
    let errorCount = 0;
    let totalBytesSent = 0;
    let totalBytesReceived = 0;

    const latencies: number[] = [];
    const statusCodes: Map<number, number> = new Map();
    const errorMessages: Map<string, number> = new Map();

    for (let i = 0; i < results.length; i++) {
        const r = results[i];
        latencies.push(r.latencyMs);

        if (r.success) {
            successCount++;
        } else {
            errorCount++;
            if (r.error) {
                errorMessages.set(r.error, (errorMessages.get(r.error) || 0) + 1);
            }
        }

        statusCodes.set(r.statusCode, (statusCodes.get(r.statusCode) || 0) + 1);

        totalBytesSent += r.bytesSent;
        totalBytesReceived += r.bytesReceived;
    }

    // Sort latencies for percentile calculation
    latencies.sort((a: number, b: number) => a - b);

    const latencyMin = latencies.length > 0 ? latencies[0] : 0;
    const latencyMax = latencies.length > 0 ? latencies[latencies.length - 1] : 0;

    let latencySum = 0;
    for (let i = 0; i < latencies.length; i++) {
        latencySum += latencies[i];
    }
    const latencyMean = latencies.length > 0 ? latencySum / latencies.length : 0;
    const latencyMedian = percentile(latencies, 50);

    const percentiles: PercentileResult = {
        p50: percentile(latencies, 50),
        p75: percentile(latencies, 75),
        p90: percentile(latencies, 90),
        p95: percentile(latencies, 95),
        p99: percentile(latencies, 99)
    };

    const requestsPerSec = totalDurationMs > 0 ? (totalRequests / totalDurationMs) * 1000 : 0;

    return {
        totalRequests,
        successCount,
        errorCount,
        totalDurationMs,
        requestsPerSec,
        latencyMin,
        latencyMax,
        latencyMean,
        latencyMedian,
        percentiles,
        statusCodes,
        errorMessages,
        totalBytesSent,
        totalBytesReceived
    };
}

/**
 * Merge results from multiple workers into a single array.
 */
export function mergeResults(workerResults: RequestResult[][]): RequestResult[] {
    const merged: RequestResult[] = [];
    for (let i = 0; i < workerResults.length; i++) {
        const results = workerResults[i];
        for (let j = 0; j < results.length; j++) {
            merged.push(results[j]);
        }
    }
    return merged;
}

/**
 * Serialize results array to JSON string for IPC transfer.
 */
export function serializeResults(results: RequestResult[]): string {
    const items: any[] = [];
    for (let i = 0; i < results.length; i++) {
        const r = results[i];
        const item: any = {
            l: Math.round(r.latencyMs * 100) / 100,
            s: r.statusCode,
            ok: r.success,
            bs: r.bytesSent,
            br: r.bytesReceived
        };
        if (r.error) item.e = r.error;
        items.push(item);
    }
    return JSON.stringify(items);
}

/**
 * Deserialize results from IPC JSON string.
 */
export function deserializeResults(json: string): RequestResult[] {
    const items = JSON.parse(json) as any[];
    const results: RequestResult[] = [];
    for (let i = 0; i < items.length; i++) {
        const item = items[i];
        const errStr: string | undefined = item.e || undefined;
        results.push({
            latencyMs: item.l,
            statusCode: item.s,
            success: item.ok,
            bytesSent: item.bs,
            bytesReceived: item.br,
            error: errStr
        });
    }
    return results;
}
