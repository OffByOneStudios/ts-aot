// Test timers module re-exports
import * as timers from 'timers';

function user_main(): number {
    console.log("Testing timers module...");

    // Test timers.setTimeout returns timer id
    const timeoutId = timers.setTimeout(() => {}, 1000);
    console.log("setTimeout returns id: " + (typeof timeoutId === "number"));

    // Test timers.clearTimeout
    timers.clearTimeout(timeoutId);
    console.log("clearTimeout called successfully");

    // Test timers.setInterval returns timer id
    const intervalId = timers.setInterval(() => {}, 1000);
    console.log("setInterval returns id: " + (typeof intervalId === "number"));

    // Test timers.clearInterval
    timers.clearInterval(intervalId);
    console.log("clearInterval called successfully");

    // Test timers.setImmediate returns timer id
    const immediateId = timers.setImmediate(() => {});
    console.log("setImmediate returns id: " + (typeof immediateId === "number"));

    // Test timers.clearImmediate
    timers.clearImmediate(immediateId);
    console.log("clearImmediate called successfully");

    console.log("All timers module tests passed!");
    return 0;
}
