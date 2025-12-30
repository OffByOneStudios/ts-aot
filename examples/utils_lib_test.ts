// Test importing from a local TypeScript library (ts-utils)
// This simulates importing from an npm package that's written in TypeScript

import { 
    chunk, compact, flatten, uniq,
    capitalize, trim, trimStart, trimEnd,
    padStart, padEnd,
    clamp, range, sum, avg, min, max 
} from './ts-utils';

console.log("=== ts-utils Library Test ===\n");

// Array utilities
console.log("--- Array utilities ---");
const numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
console.log("chunk([1-10], 3):");
const chunks = chunk(numbers, 3);
for (const c of chunks) {
    console.log("  " + c.join(", "));
}

const sparse = [0, 1, 0, 2, 0, 3];
console.log("\ncompact([0,1,0,2,0,3]): " + compact(sparse).join(", "));

const nested = [[1, 2], [3, 4], [5, 6]];
console.log("flatten([[1,2],[3,4],[5,6]]): " + flatten(nested).join(", "));

const dupes = [1, 2, 2, 3, 3, 3, 4];
console.log("uniq([1,2,2,3,3,3,4]): " + uniq(dupes).join(", "));

// String utilities
console.log("\n--- String utilities ---");
console.log("capitalize('hello'): " + capitalize("hello"));
console.log("trim('  hello  '): '" + trim("  hello  ") + "'");
console.log("trimStart('  hello'): '" + trimStart("  hello") + "'");
console.log("trimEnd('hello  '): '" + trimEnd("hello  ") + "'");
console.log("padStart('42', 5, '0'): '" + padStart("42", 5, "0") + "'");
console.log("padEnd('hi', 5, '.'): '" + padEnd("hi", 5, ".") + "'");

// Math utilities
console.log("\n--- Math utilities ---");
console.log("clamp(15, 0, 10): " + clamp(15, 0, 10));
console.log("clamp(-5, 0, 10): " + clamp(-5, 0, 10));
console.log("clamp(5, 0, 10): " + clamp(5, 0, 10));
console.log("range(1, 6): " + range(1, 6).join(", "));

const data = [10, 20, 30, 40, 50];
console.log("\ndata = [10, 20, 30, 40, 50]");
console.log("sum(data): " + sum(data));
console.log("avg(data): " + avg(data));
console.log("min(data): " + min(data));
console.log("max(data): " + max(data));

console.log("\n=== All tests passed! ===");
