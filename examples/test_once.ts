// Test once function

function once(fn: () => void): () => void {
    let called = false;
    return (): void => {
        if (!called) {
            called = true;
            fn();
        }
    };
}

let callCount = 0;
function increment(): void {
    callCount = callCount + 1;
    console.log("increment called, count=");
    console.log(callCount);
}

const onceIncrement = once(increment);

console.log("Test once:");
console.log("Calling onceIncrement 3 times...");
onceIncrement();  // Should print "increment called, count=1"
onceIncrement();  // Should NOT print anything  
onceIncrement();  // Should NOT print anything
console.log("Final count should be 1:");
console.log(callCount);
