// Simple test without generics for library import validation
import { capitalize, trim, clamp, range, sum } from './ts-utils';

console.log("=== ts-utils Simple Test ===\n");

// String utilities (no generics)
console.log("capitalize('hello'): " + capitalize("hello"));
console.log("trim('  hello  '): '" + trim("  hello  ") + "'");

// Math utilities (no generics)
console.log("clamp(15, 0, 10): " + clamp(15, 0, 10));
console.log("clamp(-5, 0, 10): " + clamp(-5, 0, 10));

// range returns number[] - should work
const nums = range(1, 6);
console.log("range(1, 6): " + nums.join(", "));

// sum takes number[] - should work
console.log("sum(range(1, 6)): " + sum(nums));

console.log("\n=== Simple test passed! ===");
