/**
 * Production API Server Example
 *
 * A minimal but production-ready REST API server demonstrating:
 * - HTTP server with routing
 * - JSON request/response handling
 * - Middleware pattern (logging, error handling)
 * - Graceful shutdown
 *
 * Usage:
 *   ts-aot server.ts -o server.exe
 *   ./server.exe
 *
 * API Endpoints:
 *   GET  /health          - Health check
 *   GET  /api/users       - List users
 *   GET  /api/users/:id   - Get user by ID
 *   POST /api/users       - Create user
 */

import * as http from 'http';
import { router, Route } from './routes/index';
import { logger } from './middleware/logger';
import { errorHandler, notFound } from './middleware/errorHandler';

// Configuration
const PORT = parseInt(process.env.PORT || '3000', 10);
const HOST = process.env.HOST || '0.0.0.0';

/**
 * Main request handler
 */
function handleRequest(req: http.IncomingMessage, res: http.ServerResponse): void {
    // Start timing for logging
    const startTime = Date.now();

    // Parse URL
    const url = req.url || '/';
    const method = req.method || 'GET';

    // Log incoming request
    logger.logRequest(method, url);

    // Collect request body for POST/PUT
    let body = '';

    req.on('data', (chunk: Buffer) => {
        body += chunk.toString();
    });

    req.on('end', () => {
        try {
            // Parse JSON body if present
            let parsedBody: any = null;
            if (body && (method === 'POST' || method === 'PUT')) {
                try {
                    parsedBody = JSON.parse(body);
                } catch (e) {
                    sendError(res, 400, 'Invalid JSON');
                    return;
                }
            }

            // Route the request
            const handled = router.route(method, url, req, res, parsedBody);

            if (!handled) {
                notFound(req, res);
            }

            // Log response
            const elapsed = Date.now() - startTime;
            logger.logResponse(method, url, res.statusCode, elapsed);

        } catch (err) {
            errorHandler(err as Error, req, res);
        }
    });

    req.on('error', (err: Error) => {
        errorHandler(err, req, res);
    });
}

/**
 * Send JSON error response
 */
function sendError(res: http.ServerResponse, status: number, message: string): void {
    res.writeHead(status, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ error: message }));
}

/**
 * Main entry point
 */
function user_main(): number {
    console.log('Production API Server');
    console.log('=====================');
    console.log('');

    const server = http.createServer(handleRequest);

    // Handle server errors
    server.on('error', (err: Error) => {
        console.error('Server error:', err.message);
        process.exit(1);
    });

    // Start listening
    server.listen(PORT, HOST, () => {
        console.log(`Server running at http://${HOST}:${PORT}`);
        console.log('');
        console.log('Endpoints:');
        console.log('  GET  /health          - Health check');
        console.log('  GET  /api/users       - List all users');
        console.log('  GET  /api/users/:id   - Get user by ID');
        console.log('  POST /api/users       - Create new user');
        console.log('');
        console.log('Press Ctrl+C to stop.');
    });

    // Graceful shutdown
    process.on('SIGINT', () => {
        console.log('');
        console.log('Shutting down gracefully...');

        server.close(() => {
            console.log('Server closed.');
            process.exit(0);
        });

        // Force exit after 5 seconds
        setTimeout(() => {
            console.log('Forcing exit...');
            process.exit(1);
        }, 5000);
    });

    return 0;
}
