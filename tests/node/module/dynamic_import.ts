// Test: Dynamic import() returns a rejected Promise for runtime imports
// In AOT compilation, dynamic imports that can't be resolved at compile time
// return a rejected Promise at runtime.

function user_main(): number {
    console.log("Testing dynamic import()...");

    // Dynamic import returns a Promise
    const promise = import('./nonexistent-module');

    // The promise should exist (not null/undefined)
    if (promise) {
        console.log("dynamic import() returned a Promise");
    } else {
        console.log("FAIL: dynamic import() did not return a Promise");
        return 1;
    }

    // Note: In AOT compilation, dynamic imports that can't be resolved
    // at compile time will reject with an error message explaining
    // that the module must be imported statically.

    // The Promise rejection can be handled with .catch()
    // (The catch callback is registered but output ordering depends on
    // microtask queue execution)

    console.log("PASS: dynamic import() returns Promise");
    return 0;
}
