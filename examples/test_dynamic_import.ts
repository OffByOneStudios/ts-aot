// Example: Dynamic import() in AOT compilation
//
// Dynamic import() returns a Promise that:
// - Resolves at compile time for statically-known modules (future enhancement)
// - Rejects at runtime for modules that can't be resolved at compile time
//
// This is an AOT limitation - true dynamic imports require runtime module loading.

function user_main(): number {
    console.log("Testing dynamic import()...");

    // Dynamic import returns a Promise
    const promise = import('./some-module');

    if (promise) {
        console.log("PASS: import() returns a Promise");
    } else {
        console.log("FAIL: import() should return a Promise");
        return 1;
    }

    return 0;
}
