// Simple string comparison test

const a = "hello";
const b = "hello";
const c = "world";

console.log("Direct string comparison:");
console.log("a === b:", a === b);  // should be true
console.log("a === c:", a === c);  // should be false

// Test with a function
function strEq(x: string, y: string): boolean {
    return x === y;
}

console.log("\nFunction comparison:");
console.log("strEq(a, b):", strEq(a, b));  // should be true
console.log("strEq(a, c):", strEq(a, c));  // should be false
