// Test events.once() and events.on() static methods
import { EventEmitter } from 'events';
import * as events from 'events';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    console.log("=== events static methods Tests ===");

    // ============================================================================
    // Test 1: events.once() - basic usage (sync test with immediate emit)
    // ============================================================================
    {
        console.log("\nTest 1: events.once() basic usage...");
        const ee = new EventEmitter();

        // Get the promise first
        const promise = events.once(ee, 'test-event');
        console.log("Promise created");

        // Emit the event immediately
        ee.emit('test-event', 'hello', 42);
        console.log("Event emitted");

        // Check that promise resolves via .then
        promise.then((result: any) => {
            console.log("Promise resolved!");
            console.log("typeof result: " + typeof result);
            console.log("Array.isArray(result): " + Array.isArray(result));
            if (result) {
                console.log("result.length: " + result.length);
            }

            // The result should be an array with the event arguments
            console.log("result.length=" + result.length);
            const len = result.length;
            console.log("len=" + len);
            console.log("len === 2: " + (len === 2));
            if (Array.isArray(result) && len === 2) {
                console.log("PASS: events.once() returns array with event args");
                console.log("result[0]=" + result[0] + ", result[1]=" + result[1]);
                passed++;
            } else {
                console.log("FAIL: events.once() wrong result type or length");
                console.log("Array.isArray=" + Array.isArray(result));
                console.log("len=" + len);
                failed++;
            }

            // Print summary
            console.log("");
            console.log("=== events static methods Results ===");
            console.log("Passed: " + passed);
            console.log("Failed: " + failed);
            console.log("Total: " + (passed + failed));
            process.exit(failed);
        });
    }

    return 0;
}
