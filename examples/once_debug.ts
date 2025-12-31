// Test mutable closures with once function - debug version

// Test 3: Simple once pattern
function once<T>(fn: () => T): () => T {
    let called = false;
    let result: T;
    console.log("once: created, called=");
    console.log(called);
    return () => {
        console.log("once inner: called=");
        console.log(called);
        if (!called) {
            console.log("once inner: not called yet, setting called=true");
            called = true;
            console.log("once inner: calling fn()");
            result = fn();
            console.log("once inner: fn() returned, result=");
            console.log(result);
        }
        console.log("once inner: returning result=");
        console.log(result);
        return result;
    };
}

let expensive = once(() => {
    console.log("Computing...");
    return 42;
});

console.log("Once test:");
console.log("First call:");
console.log(expensive());
console.log("Second call:");
console.log(expensive());
