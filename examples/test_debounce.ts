// Test debounce function

// Simple debounce implementation
function debounce(fn: () => void, delay: number): () => void {
    let timerId: number | null = null;
    return (): void => {
        if (timerId !== null) {
            clearTimeout(timerId);
        }
        timerId = setTimeout(fn, delay);
    };
}

let callCount = 0;
function logCall(): void {
    callCount = callCount + 1;
    console.log("logCall executed, count =", callCount);
}

const debouncedLog = debounce(logCall, 100);

console.log("Test debounce:");
console.log("Calling debouncedLog rapidly 3 times...");
debouncedLog();
debouncedLog();
debouncedLog();
console.log("Waiting for debounce to fire...");
// The actual call will happen after 100ms
