/**
 * HTTP Load Tester — Worker Process
 *
 * Sends HTTP requests and measures latency.
 */

import * as http from 'http';
import * as url from 'url';
import * as querystring from 'querystring';
import * as assert from 'assert';
import { RequestResult } from './stats';

export interface WorkerConfig {
    targetUrl: string;
    requestCount: number;
    concurrency: number;
    method: string;
    headers: { [key: string]: string };
    body: string;
    timeout: number;
    validateStatus: boolean;
}

/**
 * Send a single HTTP request and measure latency.
 */
function sendRequest(targetUrl: string, method: string, body: string, timeout: number): Promise<RequestResult> {
    return new Promise((resolve: (result: RequestResult) => void) => {
        const startTime = Date.now();
        const parsed = new URL(targetUrl);

        const proto = parsed.protocol;
        const isHttps = proto === 'https:';

        const portStr = parsed.port;
        const portNum = portStr !== '' ? parseInt(portStr, 10) : (isHttps ? 443 : 80);

        const options: http.RequestOptions = {
            hostname: parsed.hostname,
            port: portNum,
            path: parsed.pathname + parsed.search,
            method: method,
            timeout: timeout
        };

        const bytesSent = body ? Buffer.from(body).length : 0;
        let bytesReceived = 0;

        const req = http.request(options, (res: http.IncomingMessage) => {
            const statusCode = res.statusCode || 0;

            res.on('data', (chunk: Buffer) => {
                bytesReceived += chunk.length;
            });

            res.on('end', () => {
                const latencyMs = Date.now() - startTime;
                const success = statusCode >= 200 && statusCode < 400;

                resolve({
                    latencyMs: latencyMs,
                    statusCode: statusCode,
                    success: success,
                    bytesSent: bytesSent,
                    bytesReceived: bytesReceived
                });
            });
        });

        req.on('error', (err: Error) => {
            const latencyMs = Date.now() - startTime;
            resolve({
                latencyMs: latencyMs,
                statusCode: 0,
                success: false,
                error: err.message,
                bytesSent: bytesSent,
                bytesReceived: 0
            });
        });

        req.on('timeout', () => {
            req.destroy();
            const latencyMs = Date.now() - startTime;
            resolve({
                latencyMs: latencyMs,
                statusCode: 0,
                success: false,
                error: 'Request timeout',
                bytesSent: bytesSent,
                bytesReceived: 0
            });
        });

        if (body) {
            req.write(body);
        }
        req.end();
    });
}

/**
 * Run a batch of requests sequentially.
 */
export async function runBatch(config: WorkerConfig): Promise<RequestResult[]> {
    const results: RequestResult[] = [];

    for (let i = 0; i < config.requestCount; i++) {
        const result = await sendRequest(config.targetUrl, config.method, config.body, config.timeout);
        results.push(result);
    }

    return results;
}

/**
 * Build the target URL with optional query parameters.
 */
export function buildTargetUrl(baseUrl: string, params: { [key: string]: string }): string {
    const qs = querystring.stringify(params);
    if (qs !== '') {
        const parsed = new URL(baseUrl);
        let sep = '?';
        if (parsed.search !== '') sep = '&';
        return baseUrl + sep + qs;
    }
    return baseUrl;
}
