// Direct imports from ts-utils without barrel exports
import { clamp } from './ts-utils/math/clamp';
import { range } from './ts-utils/math/range';
import { sum } from './ts-utils/math/sum';

console.log("=== Direct Import Test ===\n");

// Test clamp
console.log("clamp(15, 0, 10): " + clamp(15, 0, 10));
console.log("clamp(-5, 0, 10): " + clamp(-5, 0, 10));
console.log("clamp(5, 0, 10): " + clamp(5, 0, 10));

// Test range
const nums = range(1, 6);
console.log("range(1, 6): " + nums.join(", "));

// Test sum
console.log("sum([1,2,3,4,5]): " + sum(nums));

console.log("\n=== All tests passed! ===");
