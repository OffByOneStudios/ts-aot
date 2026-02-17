/**
 * HTTP Load Tester
 *
 * Sends HTTP requests to a target endpoint, measures latency
 * and throughput, and generates a statistical report.
 *
 * Usage:
 *   loadtest <url> [options]
 *
 * Options:
 *   -n <num>     Total number of requests (default: 100)
 *   -c <num>     Concurrent connections (default: 10)
 *   -m <method>  HTTP method (default: GET)
 *   -d <data>    Request body data
 *   -t <ms>      Request timeout in milliseconds (default: 10000)
 *   --validate   Validate response status codes
 *   --help       Show usage
 *
 * Node modules: http, https, url, perf_hooks, os, cluster, process,
 *               dns, querystring, assert, util
 */

import * as os from 'os';
import * as cluster from 'cluster';
import * as dns from 'dns';
import * as util from 'util';
import { performance } from 'perf_hooks';
import { RequestResult, computeStats, mergeResults, serializeResults, deserializeResults } from './stats';
import { WorkerConfig, runBatch, buildTargetUrl } from './worker-proc';
import { printReport } from './reporter';

interface CliOptions {
    targetUrl: string;
    totalRequests: number;
    concurrency: number;
    workerCount: number;
    method: string;
    headers: { [key: string]: string };
    body: string;
    timeout: number;
    validateStatus: boolean;
    showHelp: boolean;
}

function printUsage(): void {
    console.log('HTTP Load Tester');
    console.log('');
    console.log('Usage: loadtest <url> [options]');
    console.log('');
    console.log('Options:');
    console.log('  -n <num>     Total requests (default: 100)');
    console.log('  -c <num>     Concurrent connections (default: 10)');
    console.log('  -m <method>  HTTP method (default: GET)');
    console.log('  -d <data>    Request body');
    console.log('  -t <ms>      Timeout in ms (default: 10000)');
    console.log('  --validate   Validate 2xx/3xx status');
    console.log('  --help       Show this help');
}

function parseOptions(argv: string[]): CliOptions {
    // Node.js convention: argv[0] = node, argv[1] = script, argv[2+] = user args
    const args = argv.slice(2);
    const opts: CliOptions = {
        targetUrl: '',
        totalRequests: 100,
        concurrency: 10,
        workerCount: 1,
        method: 'GET',
        headers: {},
        body: '',
        timeout: 10000,
        validateStatus: false,
        showHelp: false
    };

    let i = 0;
    while (i < args.length) {
        const arg = args[i];

        if (arg === '--help') {
            opts.showHelp = true;
            i++;
        } else if (arg === '--validate') {
            opts.validateStatus = true;
            i++;
        } else if (arg === '-n' && i + 1 < args.length) {
            opts.totalRequests = parseInt(args[i + 1], 10);
            if (opts.totalRequests < 1) opts.totalRequests = 100;
            i += 2;
        } else if (arg === '-c' && i + 1 < args.length) {
            opts.concurrency = parseInt(args[i + 1], 10);
            if (opts.concurrency < 1) opts.concurrency = 10;
            i += 2;
        } else if (arg === '-m' && i + 1 < args.length) {
            opts.method = args[i + 1].toUpperCase();
            i += 2;
        } else if (arg === '-H' && i + 1 < args.length) {
            const headerStr = args[i + 1];
            const colonIdx = headerStr.indexOf(':');
            if (colonIdx > 0) {
                const key = headerStr.substring(0, colonIdx).trim();
                const value = headerStr.substring(colonIdx + 1).trim();
                opts.headers[key] = value;
            }
            i += 2;
        } else if (arg === '-w' && i + 1 < args.length) {
            opts.workerCount = parseInt(args[i + 1], 10);
            if (opts.workerCount < 1) opts.workerCount = 1;
            i += 2;
        } else if (arg === '-d' && i + 1 < args.length) {
            opts.body = args[i + 1];
            i += 2;
        } else if (arg === '-t' && i + 1 < args.length) {
            opts.timeout = parseInt(args[i + 1], 10);
            if (opts.timeout < 1) opts.timeout = 10000;
            i += 2;
        } else if (!arg.startsWith('-') && opts.targetUrl === '') {
            opts.targetUrl = arg;
            i++;
        } else {
            i++;
        }
    }

    return opts;
}

/**
 * Run load test in single-process mode.
 */
async function runPrimary(opts: CliOptions): Promise<void> {
    const parsed = new URL(opts.targetUrl);

    console.log('');
    console.log('=== HTTP Load Tester ===');
    console.log('');

    // System info
    const cpuCount = os.cpus().length;
    const freeMem = Math.round(os.freemem() / (1024 * 1024));
    const totalMem = Math.round(os.totalmem() / (1024 * 1024));
    console.log('System: ' + cpuCount + ' CPUs, ' + freeMem + '/' + totalMem + ' MB memory');

    // Display test configuration
    console.log('');
    console.log('Target:      ' + opts.targetUrl);
    console.log('Method:      ' + opts.method);
    console.log('Requests:    ' + opts.totalRequests);
    console.log('Concurrency: ' + opts.concurrency);
    console.log('Timeout:     ' + opts.timeout + 'ms');
    console.log('');

    // Resolve hostname via DNS — closure captures parsed.hostname
    dns.lookup(parsed.hostname, (err: Error | null, address: string) => {
        if (err) {
            console.log('DNS resolution failed: ' + err.message);
        } else {
            console.log('Host: ' + parsed.hostname + ' -> ' + address);
        }
    });

    // Performance markers
    performance.mark('test-start');
    const startTime = performance.now();
    const startHrtime = process.hrtime.bigint();

    console.log('Running...');

    // Build worker config
    const config: WorkerConfig = {
        targetUrl: opts.targetUrl,
        requestCount: opts.totalRequests,
        concurrency: opts.concurrency,
        method: opts.method,
        headers: opts.headers,
        body: opts.body,
        timeout: opts.timeout,
        validateStatus: opts.validateStatus
    };

    // Run the load test
    const results = await runBatch(config);

    const endTime = performance.now();
    const endHrtime = process.hrtime.bigint();
    performance.mark('test-end');
    performance.measure('load-test', 'test-start', 'test-end');

    const totalDurationMs = endTime - startTime;
    const durationNs = endHrtime - startHrtime;

    console.log('Completed in ' + Math.round(totalDurationMs) + 'ms (' + durationNs + ' ns precise)');

    // Compute and print report
    const report = computeStats(results, totalDurationMs);
    printReport(report, opts.targetUrl, opts.concurrency, 1);

    // Print performance entries
    const entries = performance.getEntriesByType('measure');
    if (entries.length > 0) {
        console.log('Performance measures:');
        for (let i = 0; i < entries.length; i++) {
            const entry = entries[i];
            console.log('  ' + entry.name + ': ' + (Math.round(entry.duration * 100) / 100) + 'ms');
        }
    }

    process.exit(0);
}

/**
 * Run as cluster worker — receives config via env, sends results back via IPC.
 */
async function runAsWorker(): Promise<void> {
    const configJson = process.env.WORKER_CONFIG;
    if (!configJson) return;
    const config = JSON.parse(configJson) as WorkerConfig;

    const results = await runBatch(config);
    const serialized = serializeResults(results);

    process.send({ type: 'results', data: serialized });
}

/**
 * Run load test with cluster workers.
 */
function runWithCluster(opts: CliOptions): void {
    const parsed = new URL(opts.targetUrl);

    console.log('');
    console.log('=== HTTP Load Tester (Multi-Process) ===');
    console.log('');

    const cpuCount = os.cpus().length;
    const freeMem = Math.round(os.freemem() / (1024 * 1024));
    const totalMem = Math.round(os.totalmem() / (1024 * 1024));
    console.log('System: ' + cpuCount + ' CPUs, ' + freeMem + '/' + totalMem + ' MB memory');

    console.log('');
    console.log('Target:      ' + opts.targetUrl);
    console.log('Method:      ' + opts.method);
    console.log('Requests:    ' + opts.totalRequests);
    console.log('Concurrency: ' + opts.concurrency + ' per worker');
    console.log('Workers:     ' + opts.workerCount);
    console.log('Timeout:     ' + opts.timeout + 'ms');
    console.log('');

    performance.mark('test-start');
    const startTime = performance.now();

    // Distribute requests across workers
    const requestsPerWorker = Math.floor(opts.totalRequests / opts.workerCount);
    const remainder = opts.totalRequests % opts.workerCount;
    const allResults: RequestResult[][] = [];
    let completedWorkers = 0;

    console.log('Forking ' + opts.workerCount + ' workers...');

    for (let i = 0; i < opts.workerCount; i++) {
        let workerReqs = requestsPerWorker;
        if (i < remainder) workerReqs = workerReqs + 1;

        const worker = cluster.fork({
            WORKER_CONFIG: JSON.stringify({
                targetUrl: opts.targetUrl,
                requestCount: workerReqs,
                concurrency: opts.concurrency,
                method: opts.method,
                headers: opts.headers,
                body: opts.body,
                timeout: opts.timeout,
                validateStatus: opts.validateStatus
            })
        });

        worker.on('message', (msg: any) => {
            if (msg.type === 'results') {
                const results = deserializeResults(msg.data);
                allResults.push(results);
                completedWorkers++;
                console.log('  Worker done: ' + results.length + ' requests');

                if (completedWorkers === opts.workerCount) {
                    const endTime = performance.now();
                    performance.mark('test-end');
                    performance.measure('load-test', 'test-start', 'test-end');

                    const totalDurationMs = endTime - startTime;
                    const merged = mergeResults(allResults);
                    const report = computeStats(merged, totalDurationMs);

                    printReport(report, opts.targetUrl, opts.concurrency, opts.workerCount);
                    process.exit(0);
                }
            }
        });
    }
}

function user_main(): number {
    const opts = parseOptions(process.argv);

    if (opts.showHelp) {
        printUsage();
        return 0;
    }
    if (opts.targetUrl === '') {
        printUsage();
        return 1;
    }

    // Validate URL
    try {
        const parsed = new URL(opts.targetUrl);
        if (parsed.protocol !== 'http:' && parsed.protocol !== 'https:') {
            console.error('Error: URL must use http or https protocol');
            return 1;
        }
    } catch (err) {
        console.error('Error: invalid URL: ' + opts.targetUrl);
        return 1;
    }

    // Clamp values
    if (opts.totalRequests < 1) opts.totalRequests = 1;
    if (opts.concurrency > opts.totalRequests) opts.concurrency = opts.totalRequests;
    if (opts.concurrency < 1) opts.concurrency = 1;
    const maxWorkers = os.cpus().length;
    if (opts.workerCount > maxWorkers) opts.workerCount = maxWorkers;
    if (opts.workerCount > opts.totalRequests) opts.workerCount = opts.totalRequests;
    if (opts.workerCount < 1) opts.workerCount = 1;

    // Check if running as cluster worker
    if (cluster.isWorker) {
        runAsWorker().then(() => {
            // Worker done
        });
        return 0;
    }

    // Choose single vs multi-process mode
    if (opts.workerCount <= 1) {
        runPrimary(opts).then(() => {
            // handled by process.exit in runPrimary
        });
    } else {
        runWithCluster(opts);
    }

    return 0;
}
