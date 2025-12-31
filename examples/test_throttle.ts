// Test throttle function

// Simple throttle implementation
function throttle(fn: () => void, limit: number): () => void {
    let inThrottle = false;
    return (): void => {
        if (!inThrottle) {
            fn();
            inThrottle = true;
            setTimeout((): void => {
                inThrottle = false;
            }, limit);
        }
    };
}

let callCount = 0;
function logCall(): void {
    callCount = callCount + 1;
    console.log("logCall executed, count =", callCount);
}

const throttledLog = throttle(logCall, 50);

console.log("Test throttle:");
console.log("Calling throttledLog 5 times with small delays...");

// First call should execute immediately
throttledLog();
// These should be throttled
throttledLog();
throttledLog();

// Wait a bit then call again - should execute
setTimeout((): void => {
    console.log("After 60ms delay, calling again...");
    throttledLog();
    console.log("Final count should be 2 (first + after delay):");
    console.log(callCount);
}, 60);
