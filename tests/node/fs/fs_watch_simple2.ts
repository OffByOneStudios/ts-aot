// Simple test for fs.promises.watch() - test event loop
import * as fs from 'fs';

async function user_main(): Promise<number> {
    console.log("=== Event Loop Test ===");

    let timeoutFired = false;

    // Set up a timeout
    setTimeout(() => {
        console.log("  Timeout callback executed!");
        timeoutFired = true;
    }, 100);

    console.log("1. Timeout scheduled, waiting with a simple loop...");

    // Just wait by spinning (bad practice but for testing)
    let count = 0;
    while (!timeoutFired && count < 1000000) {
        count++;
    }

    console.log("2. Loop ended. timeoutFired=" + timeoutFired + ", count=" + count);

    // Now try awaiting a resolved promise
    console.log("3. Awaiting resolved promise...");
    const x = await Promise.resolve(42);
    console.log("4. Promise resolved, x=" + x);

    console.log("5. timeoutFired after await: " + timeoutFired);

    return timeoutFired ? 0 : 1;
}
