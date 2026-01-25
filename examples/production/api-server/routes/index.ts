/**
 * Router and Route Definitions
 *
 * Simple regex-based router for the API server.
 */

import * as http from 'http';
import { healthRoutes } from './health';
import { userRoutes } from './users';

/**
 * Route handler function type
 */
export type RouteHandler = (
    req: http.IncomingMessage,
    res: http.ServerResponse,
    params: Record<string, string>,
    body?: any
) => void;

/**
 * Route definition
 */
export interface Route {
    method: string;
    pattern: RegExp;
    paramNames: string[];
    handler: RouteHandler;
}

/**
 * Simple router class
 */
export class Router {
    private routes: Route[] = [];

    /**
     * Add a GET route
     */
    get(path: string, handler: RouteHandler): void {
        this.addRoute('GET', path, handler);
    }

    /**
     * Add a POST route
     */
    post(path: string, handler: RouteHandler): void {
        this.addRoute('POST', path, handler);
    }

    /**
     * Add a PUT route
     */
    put(path: string, handler: RouteHandler): void {
        this.addRoute('PUT', path, handler);
    }

    /**
     * Add a DELETE route
     */
    delete(path: string, handler: RouteHandler): void {
        this.addRoute('DELETE', path, handler);
    }

    /**
     * Add a route with any method
     */
    private addRoute(method: string, path: string, handler: RouteHandler): void {
        // Convert path pattern to regex
        // e.g., '/users/:id' -> /^\/users\/([^\/]+)$/
        const paramNames: string[] = [];
        let regexStr = '^';

        const parts = path.split('/');
        for (const part of parts) {
            if (part === '') continue;

            regexStr += '\\/';

            if (part.startsWith(':')) {
                // Parameter
                paramNames.push(part.substring(1));
                regexStr += '([^\\/]+)';
            } else {
                // Literal
                regexStr += part.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
            }
        }

        regexStr += '$';

        this.routes.push({
            method,
            pattern: new RegExp(regexStr),
            paramNames,
            handler
        });
    }

    /**
     * Route a request
     * Returns true if a route matched, false otherwise
     */
    route(
        method: string,
        url: string,
        req: http.IncomingMessage,
        res: http.ServerResponse,
        body?: any
    ): boolean {
        // Parse URL path (remove query string)
        const pathEnd = url.indexOf('?');
        const path = pathEnd >= 0 ? url.substring(0, pathEnd) : url;

        for (const route of this.routes) {
            if (route.method !== method) continue;

            const match = path.match(route.pattern);
            if (match) {
                // Extract parameters
                const params: Record<string, string> = {};
                for (let i = 0; i < route.paramNames.length; i++) {
                    params[route.paramNames[i]] = match[i + 1];
                }

                route.handler(req, res, params, body);
                return true;
            }
        }

        return false;
    }
}

// Create and configure router
export const router = new Router();

// Register routes
healthRoutes(router);
userRoutes(router);
