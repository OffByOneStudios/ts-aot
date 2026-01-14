// Test EventEmitter.listeners() method
import { EventEmitter } from 'events';

function user_main(): number {
    console.log("=== EventEmitter.listeners() Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: listeners() returns empty array for non-existent event
    console.log("\n1. listeners() for non-existent event:");
    const emitter1 = new EventEmitter();
    const noListeners = emitter1.listeners("nonexistent");
    console.log("  Got array length: " + noListeners.length);
    if (noListeners.length === 0) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 0)");
        failed++;
    }

    // Test 2: listeners() returns array of registered listeners
    console.log("\n2. listeners() after registering listeners:");
    const emitter2 = new EventEmitter();
    const listener1 = () => console.log("first");
    const listener2 = () => console.log("second");
    emitter2.on("data", listener1);
    emitter2.on("data", listener2);
    const dataListeners = emitter2.listeners("data");
    console.log("  Got array length: " + dataListeners.length);
    if (dataListeners.length === 2) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 2)");
        failed++;
    }

    // Test 3: listeners() returns a copy (not the internal array)
    console.log("\n3. listeners() returns a copy:");
    const emitter3 = new EventEmitter();
    emitter3.on("test", () => {});
    const listeners1 = emitter3.listeners("test");
    const listeners2 = emitter3.listeners("test");
    // Both should have length 1, and adding to one shouldn't affect the other
    const len1 = listeners1.length;
    const len2 = listeners2.length;
    console.log("  Copy 1 length: " + len1 + ", Copy 2 length: " + len2);
    if (len1 === 1 && len2 === 1) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 4: listeners() after removing a listener
    console.log("\n4. listeners() after removeListener:");
    const emitter4 = new EventEmitter();
    const handler = () => {};
    emitter4.on("event", handler);
    emitter4.on("event", () => {});
    console.log("  Before remove: " + emitter4.listeners("event").length);
    emitter4.removeListener("event", handler);
    const afterRemove = emitter4.listeners("event");
    console.log("  After remove: " + afterRemove.length);
    if (afterRemove.length === 1) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 1)");
        failed++;
    }

    // Test 5: listeners() for different events are independent
    console.log("\n5. listeners() for different events:");
    const emitter5 = new EventEmitter();
    emitter5.on("eventA", () => {});
    emitter5.on("eventA", () => {});
    emitter5.on("eventB", () => {});
    const listenersA = emitter5.listeners("eventA");
    const listenersB = emitter5.listeners("eventB");
    console.log("  eventA: " + listenersA.length + ", eventB: " + listenersB.length);
    if (listenersA.length === 2 && listenersB.length === 1) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");
    if (failed === 0) {
        console.log("All EventEmitter.listeners() tests passed!");
    }

    return failed;
}
