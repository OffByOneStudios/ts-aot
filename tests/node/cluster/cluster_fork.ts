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
        } else {
            console.log("FAIL: Failed to fork worker");
            return 1;
        }

        console.log("=== Master tests passed! ===");
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
    }

    return 0;
}
