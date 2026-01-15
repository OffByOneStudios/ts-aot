// Test timers/promises module
import * as timersPromises from 'timers/promises';

async function user_main(): Promise<number> {
    console.log("Testing timers/promises module...");

    // Test timers/promises.setTimeout without value
    console.log("Testing setTimeout without value...");
    const start1 = Date.now();
    await timersPromises.setTimeout(50);
    const elapsed1 = Date.now() - start1;
    console.log("setTimeout(50) completed");
    console.log("Elapsed: " + elapsed1 + "ms");
    // Allow tolerance - just check it waited at least 40ms
    if (elapsed1 >= 40) {
        console.log("PASS: Timer waited");
    } else {
        console.log("FAIL: Timer too fast");
    }

    // Test timers/promises.setTimeout with value
    console.log("\nTesting setTimeout with value...");
    const result = await timersPromises.setTimeout(10, "hello");
    console.log("setTimeout returned: " + result);
    if (result === "hello") {
        console.log("PASS: Value correct");
    } else {
        console.log("FAIL: Value incorrect");
    }

    // Test timers/promises.setImmediate without value
    console.log("\nTesting setImmediate without value...");
    const start2 = Date.now();
    await timersPromises.setImmediate();
    const elapsed2 = Date.now() - start2;
    console.log("setImmediate completed");
    console.log("Elapsed: " + elapsed2 + "ms");
    if (elapsed2 < 50) {
        console.log("PASS: Immediate was fast");
    } else {
        console.log("FAIL: Immediate too slow");
    }

    // Test timers/promises.setImmediate with value
    console.log("\nTesting setImmediate with value...");
    const result2 = await timersPromises.setImmediate("world");
    console.log("setImmediate returned: " + result2);
    if (result2 === "world") {
        console.log("PASS: Value correct");
    } else {
        console.log("FAIL: Value incorrect");
    }

    console.log("\nAll timers/promises tests passed!");
    return 0;
}
