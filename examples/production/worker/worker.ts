/**
 * Production Background Worker Example
 *
 * A background job processor demonstrating:
 * - Timer-based scheduling
 * - Job queue processing
 * - Error recovery
 * - Graceful shutdown
 * - Logging
 *
 * Usage:
 *   ts-aot worker.ts -o worker.exe
 *   ./worker.exe
 */

import { Job, JobQueue } from './scheduler';
import { cleanupJob } from './jobs/cleanup';
import { reportJob } from './jobs/report';

// Configuration
const POLL_INTERVAL = 1000;  // Check for jobs every 1 second
const MAX_RETRIES = 3;
const SHUTDOWN_TIMEOUT = 5000;

// State
let isRunning = true;
let activeJob: Job | null = null;

/**
 * Logger utility
 */
const logger = {
    info(message: string): void {
        const timestamp = new Date().toISOString();
        console.log(`[${timestamp}] INFO  ${message}`);
    },

    warn(message: string): void {
        const timestamp = new Date().toISOString();
        console.log(`[${timestamp}] WARN  ${message}`);
    },

    error(message: string): void {
        const timestamp = new Date().toISOString();
        console.error(`[${timestamp}] ERROR ${message}`);
    },

    debug(message: string): void {
        const timestamp = new Date().toISOString();
        console.log(`[${timestamp}] DEBUG ${message}`);
    }
};

/**
 * Process a single job
 */
async function processJob(job: Job): Promise<boolean> {
    logger.info(`Processing job: ${job.type} (id: ${job.id})`);

    try {
        activeJob = job;
        job.status = 'running';
        job.startedAt = Date.now();

        // Execute the job
        await job.handler(job.data);

        job.status = 'completed';
        job.completedAt = Date.now();
        logger.info(`Job completed: ${job.type} (id: ${job.id}) in ${job.completedAt - job.startedAt}ms`);

        return true;
    } catch (err) {
        const error = err as Error;
        job.status = 'failed';
        job.error = error.message;
        job.retries = (job.retries || 0) + 1;

        logger.error(`Job failed: ${job.type} (id: ${job.id}) - ${error.message}`);

        // Retry if under limit
        if (job.retries < MAX_RETRIES) {
            logger.warn(`Scheduling retry ${job.retries}/${MAX_RETRIES} for job ${job.id}`);
            job.status = 'pending';
            job.scheduledAt = Date.now() + (job.retries * 5000);  // Exponential backoff
            return false;
        }

        logger.error(`Job ${job.id} exceeded max retries, marking as failed`);
        return false;
    } finally {
        activeJob = null;
    }
}

/**
 * Main worker loop
 */
async function workerLoop(queue: JobQueue): Promise<void> {
    logger.info('Worker loop started');

    while (isRunning) {
        // Get next ready job
        const job = queue.getNextJob();

        if (job) {
            await processJob(job);
        }

        // Wait before next poll
        await new Promise(resolve => setTimeout(resolve, POLL_INTERVAL));
    }

    logger.info('Worker loop stopped');
}

/**
 * Schedule periodic jobs
 */
function schedulePeriodicJobs(queue: JobQueue): void {
    // Schedule cleanup job every 30 seconds
    const scheduleCleanup = () => {
        if (!isRunning) return;

        queue.addJob({
            id: 'cleanup-' + Date.now(),
            type: 'cleanup',
            data: { maxAge: 3600000 },  // 1 hour
            handler: cleanupJob,
            status: 'pending',
            scheduledAt: Date.now()
        });

        setTimeout(scheduleCleanup, 30000);
    };

    // Schedule report job every minute
    const scheduleReport = () => {
        if (!isRunning) return;

        queue.addJob({
            id: 'report-' + Date.now(),
            type: 'report',
            data: { includeStats: true },
            handler: reportJob,
            status: 'pending',
            scheduledAt: Date.now()
        });

        setTimeout(scheduleReport, 60000);
    };

    // Start scheduling after initial delay
    setTimeout(scheduleCleanup, 5000);
    setTimeout(scheduleReport, 10000);
}

/**
 * Graceful shutdown handler
 */
function shutdown(): void {
    logger.info('Shutdown requested...');
    isRunning = false;

    if (activeJob) {
        logger.warn(`Waiting for active job: ${activeJob.type} (id: ${activeJob.id})`);
    }

    // Force exit after timeout
    setTimeout(() => {
        logger.warn('Forcing shutdown...');
        process.exit(1);
    }, SHUTDOWN_TIMEOUT);
}

/**
 * Main entry point
 */
function user_main(): number {
    console.log('Background Worker');
    console.log('=================');
    console.log('');

    // Create job queue
    const queue = new JobQueue();

    // Register shutdown handlers
    process.on('SIGINT', shutdown);
    process.on('SIGTERM', shutdown);

    logger.info('Worker starting...');

    // Add some initial jobs
    queue.addJob({
        id: 'startup-cleanup',
        type: 'cleanup',
        data: { maxAge: 86400000 },  // 24 hours
        handler: cleanupJob,
        status: 'pending',
        scheduledAt: Date.now()
    });

    // Schedule periodic jobs
    schedulePeriodicJobs(queue);

    // Start worker loop
    workerLoop(queue).then(() => {
        logger.info('Worker shutdown complete');
        process.exit(0);
    });

    logger.info('Worker is running. Press Ctrl+C to stop.');

    return 0;
}
