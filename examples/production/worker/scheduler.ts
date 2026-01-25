/**
 * Job Scheduler
 *
 * Simple in-memory job queue with scheduling support.
 */

/**
 * Job status
 */
export type JobStatus = 'pending' | 'running' | 'completed' | 'failed';

/**
 * Job definition
 */
export interface Job {
    id: string;
    type: string;
    data: any;
    handler: (data: any) => Promise<void>;
    status: JobStatus;
    scheduledAt: number;
    startedAt?: number;
    completedAt?: number;
    error?: string;
    retries?: number;
}

/**
 * Job queue
 */
export class JobQueue {
    private jobs: Map<string, Job> = new Map();
    private pendingJobs: Job[] = [];

    /**
     * Add a job to the queue
     */
    addJob(job: Job): void {
        this.jobs.set(job.id, job);

        if (job.status === 'pending') {
            this.pendingJobs.push(job);
            // Sort by scheduled time
            this.pendingJobs.sort((a, b) => a.scheduledAt - b.scheduledAt);
        }
    }

    /**
     * Get the next job ready to run
     */
    getNextJob(): Job | null {
        const now = Date.now();

        // Find first job that is ready
        for (let i = 0; i < this.pendingJobs.length; i++) {
            const job = this.pendingJobs[i];

            if (job.status === 'pending' && job.scheduledAt <= now) {
                // Remove from pending list
                this.pendingJobs.splice(i, 1);
                return job;
            }
        }

        return null;
    }

    /**
     * Get job by ID
     */
    getJob(id: string): Job | undefined {
        return this.jobs.get(id);
    }

    /**
     * Get all jobs of a specific status
     */
    getJobsByStatus(status: JobStatus): Job[] {
        const result: Job[] = [];
        this.jobs.forEach(job => {
            if (job.status === status) {
                result.push(job);
            }
        });
        return result;
    }

    /**
     * Get queue statistics
     */
    getStats(): { pending: number; running: number; completed: number; failed: number; total: number } {
        let pending = 0;
        let running = 0;
        let completed = 0;
        let failed = 0;

        this.jobs.forEach(job => {
            switch (job.status) {
                case 'pending': pending++; break;
                case 'running': running++; break;
                case 'completed': completed++; break;
                case 'failed': failed++; break;
            }
        });

        return {
            pending,
            running,
            completed,
            failed,
            total: this.jobs.size
        };
    }

    /**
     * Remove completed jobs older than specified age
     */
    cleanup(maxAge: number): number {
        const cutoff = Date.now() - maxAge;
        let removed = 0;

        const toRemove: string[] = [];

        this.jobs.forEach((job, id) => {
            if (job.status === 'completed' || job.status === 'failed') {
                const completedAt = job.completedAt || job.startedAt || 0;
                if (completedAt < cutoff) {
                    toRemove.push(id);
                }
            }
        });

        for (const id of toRemove) {
            this.jobs.delete(id);
            removed++;
        }

        return removed;
    }

    /**
     * Get number of pending jobs
     */
    get pendingCount(): number {
        return this.pendingJobs.length;
    }

    /**
     * Get total number of jobs
     */
    get size(): number {
        return this.jobs.size;
    }
}

/**
 * Schedule a job to run after a delay
 */
export function scheduleJob(
    queue: JobQueue,
    type: string,
    data: any,
    handler: (data: any) => Promise<void>,
    delayMs: number = 0
): string {
    const id = `${type}-${Date.now()}-${Math.random().toString(36).substring(7)}`;

    const job: Job = {
        id,
        type,
        data,
        handler,
        status: 'pending',
        scheduledAt: Date.now() + delayMs
    };

    queue.addJob(job);
    return id;
}

/**
 * Schedule a recurring job
 */
export function scheduleRecurringJob(
    queue: JobQueue,
    type: string,
    data: any,
    handler: (data: any) => Promise<void>,
    intervalMs: number
): () => void {
    let cancelled = false;

    const schedule = () => {
        if (cancelled) return;

        scheduleJob(queue, type, data, async (d) => {
            await handler(d);
            // Schedule next occurrence
            setTimeout(schedule, intervalMs);
        }, 0);
    };

    // Start the first occurrence
    schedule();

    // Return cancel function
    return () => { cancelled = true; };
}
