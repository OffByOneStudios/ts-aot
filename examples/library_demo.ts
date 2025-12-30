// Test that demonstrates working library import
// Use cases that work: direct function call in expressions

import { add, multiply, square } from './simple_lib';

console.log("=== Library Import Demo ===\n");

// These work - functions used directly in string concat
console.log("add(10, 20) = " + add(10, 20));
console.log("multiply(7, 8) = " + multiply(7, 8));
console.log("square(9) = " + square(9));

// Can use in arithmetic expressions that get printed directly
console.log("add(1, 2) + add(3, 4) = " + (add(1, 2) + add(3, 4)));

console.log("\n=== Success! ===");
