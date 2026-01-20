// Test perf_hooks eventLoopUtilization
import { performance } from 'perf_hooks';

function user_main(): number {
    console.log("=== perf_hooks eventLoopUtilization test ===");

    // Test 1: Basic eventLoopUtilization call
    console.log("Test 1: Basic eventLoopUtilization...");
    const elu1 = performance.eventLoopUtilization();
    console.log("ELU idle >= 0: " + (elu1.idle >= 0));
    console.log("ELU active >= 0: " + (elu1.active >= 0));
    console.log("ELU utilization >= 0: " + (elu1.utilization >= 0));
    console.log("ELU utilization <= 1: " + (elu1.utilization <= 1));

    // Test 2: Get another reading and compare
    console.log("Test 2: Compare two readings...");
    const elu2 = performance.eventLoopUtilization();
    console.log("ELU2 active >= ELU1 active: " + (elu2.active >= elu1.active));

    // Test 3: Differential between two ELU objects
    console.log("Test 3: Differential ELU...");
    const diff = performance.eventLoopUtilization(elu1, elu2);
    console.log("Diff has valid idle: " + (diff.idle >= 0));
    console.log("Diff has valid active: " + (diff.active >= 0));

    console.log("=== eventLoopUtilization tests passed ===");
    return 0;
}
