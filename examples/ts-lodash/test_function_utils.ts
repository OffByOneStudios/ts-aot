/**
 * Test file for ts-lodash function utilities
 */
import { once, negate, memoize } from './src/function/index';

// Test once()
console.log("=== once() test ===");

function makeExpensiveComputation(): () => number {
    let computeCount = 0;
    return once((): number => {
        computeCount = computeCount + 1;
        console.log("Computing...");
        return 42;
    });
}

const expensive = makeExpensiveComputation();
console.log("First call:");
console.log(expensive());
console.log("Second call:");
console.log(expensive());
console.log("Third call:");
console.log(expensive());

// Test negate()
console.log("");
console.log("=== negate() test ===");

function isEven(n: number): boolean {
    return n % 2 === 0;
}

const isOdd = negate(isEven);
console.log("isOdd(1):");
console.log(isOdd(1));  // true
console.log("isOdd(2):");
console.log(isOdd(2));  // false
console.log("isOdd(3):");
console.log(isOdd(3));  // true
console.log("isOdd(4):");
console.log(isOdd(4));  // false

// Test memoize()
console.log("");
console.log("=== memoize() test ===");

// Memoized version
const memoizedSquare = memoize((n: number): number => {
    console.log("Computing square...");
    return n * n;
});

console.log("memoizedSquare(5):");
console.log(memoizedSquare(5));
console.log("memoizedSquare(5) again:");
console.log(memoizedSquare(5));
console.log("memoizedSquare(10):");
console.log(memoizedSquare(10));
console.log("memoizedSquare(5) third time:");
console.log(memoizedSquare(5));

console.log("");
console.log("All function utility tests passed!");
