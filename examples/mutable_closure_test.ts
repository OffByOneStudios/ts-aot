// Test mutable closures - capture by reference

// Test 1: Counter pattern
function makeCounter(): () => number {
    let count = 0;
    return () => {
        count = count + 1;
        return count;
    };
}

let counter = makeCounter();
console.log("Counter test:");
console.log("First call:", counter());   // Should be 1
console.log("Second call:", counter());  // Should be 2
console.log("Third call:", counter());   // Should be 3

// Test 2: Outer mutation after closure creation
function testOuterMutation(): () => number {
    let x = 10;
    let getter = () => x;
    x = 20;  // Mutate AFTER closure creation
    return getter;
}

let getter = testOuterMutation();
console.log("\nOuter mutation test:");
console.log("Should be 20:", getter());  // Should see 20, not 10

// Test 3: Simple once pattern
function once<T>(fn: () => T): () => T {
    let called = false;
    let result: T;
    return () => {
        if (!called) {
            called = true;
            result = fn();
        }
        return result;
    };
}

let expensive = once(() => {
    console.log("Computing...");
    return 42;
});

console.log("\nOnce test:");
console.log("First:", expensive());  // Should print "Computing..." then 42
console.log("Second:", expensive()); // Should just print 42 (no "Computing...")
console.log("Third:", expensive());  // Should just print 42

console.log("\nAll mutable closure tests done!");
