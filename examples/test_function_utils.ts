// Test function utilities for Epic 105.5

// 1. once - ensures function is only called once
function once(fn: () => void): () => void {
    let called = false;
    return (): void => {
        if (!called) {
            called = true;
            fn();
        }
    };
}

// 2. negate - negates a predicate function
function negate(fn: (x: number) => boolean): (x: number) => boolean {
    return (x: number): boolean => {
        let res = fn(x);
        return !res;
    };
}

// Test once
console.log("=== Test once ===");
let callCount = 0;
function trackCalls(): void {
    callCount = callCount + 1;
}
const onceTrack = once(trackCalls);
onceTrack();
onceTrack();
onceTrack();
console.log("Call count (should be 1):");
console.log(callCount);

// Test negate
console.log("=== Test negate ===");
function isEven(n: number): boolean {
    return n % 2 === 0;
}
const isOdd = negate(isEven);
console.log("isOdd(1) (should be true):");
console.log(isOdd(1));
console.log("isOdd(2) (should be false):");
console.log(isOdd(2));

console.log("=== All tests complete ===");
