/**
 * Error Handler Middleware
 *
 * Centralized error handling for the API server.
 */

import * as http from 'http';
import { logger } from './logger';

/**
 * Handle application errors
 */
export function errorHandler(
    err: Error,
    req: http.IncomingMessage,
    res: http.ServerResponse
): void {
    const method = req.method || 'UNKNOWN';
    const url = req.url || '/';

    // Log the error
    logger.logError(method, url, err);

    // Send error response
    const status = 500;
    const response = {
        error: 'Internal Server Error',
        message: err.message
    };

    // Don't leak stack traces in production
    if (process.env.NODE_ENV === 'development') {
        (response as any).stack = err.stack;
    }

    res.writeHead(status, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(response));
}

/**
 * Handle 404 Not Found
 */
export function notFound(
    req: http.IncomingMessage,
    res: http.ServerResponse
): void {
    const method = req.method || 'UNKNOWN';
    const url = req.url || '/';

    res.writeHead(404, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
        error: 'Not Found',
        message: `Cannot ${method} ${url}`
    }));
}
