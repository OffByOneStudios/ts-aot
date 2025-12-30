// Simple import test with more functions (no loops for now)
import { add, multiply, square } from './simple_lib';

console.log("=== Simple Library Import Test ===\n");

// Test basic functions
console.log("add(2, 3): " + add(2, 3));
console.log("multiply(4, 5): " + multiply(4, 5));
console.log("square(6): " + square(6));

// Test in expressions
const result = add(multiply(2, 3), square(2));
console.log("add(multiply(2, 3), square(2)): " + result);

// Test nested calls
const nested = add(add(1, 2), add(3, 4));
console.log("add(add(1, 2), add(3, 4)): " + nested);

// Test with variables
const a = 10;
const b = 5;
console.log("add(10, 5): " + add(a, b));
console.log("multiply(10, 5): " + multiply(a, b));

console.log("\n=== All tests passed! ===");
