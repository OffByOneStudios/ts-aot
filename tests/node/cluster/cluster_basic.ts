// Test basic cluster module functionality

import * as cluster from 'cluster';

function user_main(): number {
    console.log("=== Testing cluster module ===");

    // Test 1: Check isMaster/isPrimary (should be true in parent process)
    console.log("Test 1: Check isMaster/isPrimary");
    if (cluster.isMaster) {
        console.log("PASS: cluster.isMaster is true");
    } else {
        console.log("FAIL: cluster.isMaster should be true");
        return 1;
    }

    if (cluster.isPrimary) {
        console.log("PASS: cluster.isPrimary is true");
    } else {
        console.log("FAIL: cluster.isPrimary should be true");
        return 1;
    }

    // Test 2: Check isWorker (should be false in parent process)
    console.log("Test 2: Check isWorker");
    if (!cluster.isWorker) {
        console.log("PASS: cluster.isWorker is false");
    } else {
        console.log("FAIL: cluster.isWorker should be false");
        return 1;
    }

    // Test 3: Check workers map exists
    console.log("Test 3: Check workers map");
    const workers = cluster.workers;
    if (workers) {
        console.log("PASS: cluster.workers exists");
    } else {
        console.log("FAIL: cluster.workers should exist");
        return 1;
    }

    // Test 4: Check settings exists
    console.log("Test 4: Check settings");
    const settings = cluster.settings;
    if (settings) {
        console.log("PASS: cluster.settings exists");
    } else {
        console.log("FAIL: cluster.settings should exist");
        return 1;
    }

    // Test 5: Check worker is undefined in master
    console.log("Test 5: Check worker is undefined in master");
    const worker = cluster.worker;
    if (!worker) {
        console.log("PASS: cluster.worker is undefined in master");
    } else {
        console.log("FAIL: cluster.worker should be undefined in master");
        return 1;
    }

    // Test 6: setupPrimary can be called without error
    console.log("Test 6: Call setupPrimary");
    cluster.setupPrimary();
    console.log("PASS: setupPrimary called successfully");

    // Test 7: setupMaster alias works
    console.log("Test 7: Call setupMaster");
    cluster.setupMaster();
    console.log("PASS: setupMaster called successfully");

    console.log("\n=== All cluster basic tests passed! ===");
    return 0;
}
