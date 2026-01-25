/**
 * Request Logger Middleware
 *
 * Simple request/response logging for the API server.
 */

/**
 * Logger utility
 */
export const logger = {
    /**
     * Log an incoming request
     */
    logRequest(method: string, url: string): void {
        const timestamp = new Date().toISOString();
        console.log(`[${timestamp}] --> ${method} ${url}`);
    },

    /**
     * Log a response
     */
    logResponse(method: string, url: string, status: number, elapsed: number): void {
        const timestamp = new Date().toISOString();
        const statusText = status >= 400 ? 'ERROR' : 'OK';
        console.log(`[${timestamp}] <-- ${method} ${url} ${status} ${statusText} (${elapsed}ms)`);
    },

    /**
     * Log an error
     */
    logError(method: string, url: string, error: Error): void {
        const timestamp = new Date().toISOString();
        console.error(`[${timestamp}] ERR ${method} ${url}: ${error.message}`);
    },

    /**
     * Log a general message
     */
    log(message: string): void {
        const timestamp = new Date().toISOString();
        console.log(`[${timestamp}] ${message}`);
    }
};
