// Simple test for fs.promises.watch() - test event loop with unresolved promise
import * as fs from 'fs';

async function user_main(): Promise<number> {
    console.log("=== Event Loop Test with Timeout ===");

    let timeoutFired = false;
    let resolvePromise: (value: number) => void;

    // Create an unresolved promise
    const pendingPromise = new Promise<number>((resolve) => {
        resolvePromise = resolve;
    });

    // Set up a timeout that resolves the promise
    setTimeout(() => {
        console.log("  Timeout callback executed!");
        timeoutFired = true;
        resolvePromise(42);
    }, 100);

    console.log("1. Timeout scheduled, awaiting pending promise...");

    // Await the pending promise - this should run the event loop
    const x = await pendingPromise;
    console.log("2. Promise resolved, x=" + x);

    console.log("3. timeoutFired: " + timeoutFired);

    return timeoutFired ? 0 : 1;
}
