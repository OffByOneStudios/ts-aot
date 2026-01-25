/**
 * Health Check Routes
 */

import * as http from 'http';
import { Router, RouteHandler } from './index';

const startTime = Date.now();

/**
 * Health check handler
 */
const getHealth: RouteHandler = (req, res, params) => {
    const uptime = Math.floor((Date.now() - startTime) / 1000);

    const health = {
        status: 'healthy',
        uptime: uptime,
        timestamp: new Date().toISOString(),
        version: '1.0.0'
    };

    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(health));
};

/**
 * Register health routes
 */
export function healthRoutes(router: Router): void {
    router.get('/health', getHealth);
}
