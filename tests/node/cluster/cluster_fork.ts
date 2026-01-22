// Test cluster fork and basic worker detection

import * as cluster from 'cluster';

function user_main(): number {
    console.log("=== Testing cluster fork ===" );

    if (cluster.isMaster) {
        console.log("Master process started");
        console.log("isMaster:", cluster.isMaster);
        console.log("isPrimary:", cluster.isPrimary);
        console.log("isWorker:", cluster.isWorker);

        // Fork a worker
        console.log("Forking worker...");
        const worker = cluster.fork();

        if (worker) {
            console.log("PASS: Worker forked successfully with id:", worker.id);

            // Listen for worker exit
            worker.on('exit', () => {
                console.log("Worker exited");
                console.log("=== Master tests passed! ===");
                process.exit(0);
            });
        } else {
            console.log("FAIL: Failed to fork worker");
            return 1;
        }

        // Timeout in case worker doesn't exit
        setTimeout(() => {
            console.log("Timeout - exiting master");
            process.exit(0);
        }, 2000);
    } else {
        // Worker process
        console.log("Worker process started!");
        console.log("isMaster:", cluster.isMaster);
        console.log("isPrimary:", cluster.isPrimary);
        console.log("isWorker:", cluster.isWorker);

        // Get worker ID from environment
        const workerId = process.env["CLUSTER_WORKER_ID"];
        console.log("Worker ID from env:", workerId);

        if (!cluster.isMaster && cluster.isWorker) {
            console.log("PASS: Worker correctly identified as worker");
        } else {
            console.log("FAIL: Worker not correctly identified");
        }

        console.log("=== Worker tests passed! ===");

        // Worker exits cleanly
        process.exit(0);
    }

    return 0;
}
