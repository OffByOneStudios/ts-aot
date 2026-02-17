/**
 * HTTP Load Tester — Report Generator
 *
 * Formats test results into a human-readable summary table
 * with latency histograms and status code distribution.
 */

import { StatsReport } from './stats';

/**
 * Format a number to a fixed number of decimal places.
 */
function fixed(n: number, decimals: number): string {
    const factor = Math.pow(10, decimals);
    const rounded = Math.round(n * factor) / factor;
    return '' + rounded;
}

/**
 * Format milliseconds with appropriate units.
 */
function formatDuration(ms: number): string {
    if (ms < 1) return fixed(ms * 1000, 1) + 'us';
    if (ms < 1000) return fixed(ms, 2) + 'ms';
    return fixed(ms / 1000, 2) + 's';
}

/**
 * Format bytes with appropriate units.
 */
function formatBytes(bytes: number): string {
    if (bytes === 0) return '0 B';
    const units = ['B', 'KB', 'MB', 'GB'];
    let idx = 0;
    let size = bytes;
    while (size >= 1024 && idx < units.length - 1) {
        size = size / 1024;
        idx++;
    }
    return fixed(size, 2) + ' ' + units[idx];
}

/**
 * Pad a string to a minimum width.
 */
function padRight(s: string, width: number): string {
    let result = s;
    while (result.length < width) {
        result = result + ' ';
    }
    return result;
}

function padLeft(s: string, width: number): string {
    let result = s;
    while (result.length < width) {
        result = ' ' + result;
    }
    return result;
}

/**
 * Print a separator line.
 */
function separator(width: number): string {
    let line = '';
    for (let i = 0; i < width; i++) {
        line += '-';
    }
    return line;
}

/**
 * Generate and print the full load test report.
 */
export function printReport(report: StatsReport, targetUrl: string, concurrency: number, workerCount: number): void {
    const WIDTH = 60;

    console.log('');
    console.log(separator(WIDTH));
    console.log('  HTTP Load Test Report');
    console.log(separator(WIDTH));
    console.log('');

    // Target info
    console.log('  Target:       ' + targetUrl);
    console.log('  Concurrency:  ' + concurrency);
    console.log('  Workers:      ' + workerCount);
    console.log('');

    // Summary
    console.log(separator(WIDTH));
    console.log('  Summary');
    console.log(separator(WIDTH));
    console.log('  Total requests:    ' + padLeft('' + report.totalRequests, 10));
    console.log('  Successful:        ' + padLeft('' + report.successCount, 10));
    console.log('  Failed:            ' + padLeft('' + report.errorCount, 10));
    console.log('  Duration:          ' + padLeft(formatDuration(report.totalDurationMs), 10));
    console.log('  Requests/sec:      ' + padLeft(fixed(report.requestsPerSec, 2), 10));
    console.log('');

    // Latency
    console.log(separator(WIDTH));
    console.log('  Latency Distribution');
    console.log(separator(WIDTH));
    console.log('  Min:      ' + padLeft(formatDuration(report.latencyMin), 12));
    console.log('  Max:      ' + padLeft(formatDuration(report.latencyMax), 12));
    console.log('  Mean:     ' + padLeft(formatDuration(report.latencyMean), 12));
    console.log('  Median:   ' + padLeft(formatDuration(report.latencyMedian), 12));
    console.log('');

    // Percentiles
    console.log(separator(WIDTH));
    console.log('  Percentiles');
    console.log(separator(WIDTH));
    console.log('  p50:   ' + padLeft(formatDuration(report.percentiles.p50), 12));
    console.log('  p75:   ' + padLeft(formatDuration(report.percentiles.p75), 12));
    console.log('  p90:   ' + padLeft(formatDuration(report.percentiles.p90), 12));
    console.log('  p95:   ' + padLeft(formatDuration(report.percentiles.p95), 12));
    console.log('  p99:   ' + padLeft(formatDuration(report.percentiles.p99), 12));
    console.log('');

    // Status codes
    console.log(separator(WIDTH));
    console.log('  Status Code Distribution');
    console.log(separator(WIDTH));
    const statusKeys = Array.from(report.statusCodes.keys());
    statusKeys.sort((a: number, b: number) => a - b);
    for (let i = 0; i < statusKeys.length; i++) {
        const code = statusKeys[i];
        const count = report.statusCodes.get(code) || 0;
        const pct = report.totalRequests > 0 ? fixed((count / report.totalRequests) * 100, 1) : '0';
        console.log('  ' + padRight('' + code, 4) + ':   ' + padLeft('' + count, 8) + '  (' + pct + '%)');
    }
    console.log('');

    // Transfer
    console.log(separator(WIDTH));
    console.log('  Transfer');
    console.log(separator(WIDTH));
    console.log('  Sent:       ' + padLeft(formatBytes(report.totalBytesSent), 12));
    console.log('  Received:   ' + padLeft(formatBytes(report.totalBytesReceived), 12));
    console.log('');

    // Errors (if any)
    const errKeys = Array.from(report.errorMessages.keys());
    if (errKeys.length > 0) {
        console.log(separator(WIDTH));
        console.log('  Error Details');
        console.log(separator(WIDTH));
        for (let i = 0; i < errKeys.length; i++) {
            const errMsg = errKeys[i];
            const errCount = report.errorMessages.get(errMsg) || 0;
            console.log('  [' + errCount + '] ' + errMsg);
        }
        console.log('');
    }

    // Error rate
    const errorRate = report.totalRequests > 0 ? (report.errorCount / report.totalRequests) * 100 : 0;
    if (errorRate > 0) {
        console.log('  Error rate: ' + fixed(errorRate, 2) + '%');
    } else {
        console.log('  Error rate: 0% (all requests succeeded)');
    }

    console.log(separator(WIDTH));
}
