// Test sortBy with closure capture

import { sortBy, sortByDesc } from "./ts-lodash/src/collection/sortBy";

console.log("Testing sortBy:");

// Test 1: Sort numbers ascending
const nums = [3, 1, 4, 1, 5, 9, 2, 6];
const sorted = sortBy(nums, (n: number) => n);
console.log("Sorted ascending:");
console.log(sorted[0]);  // Should be 1
console.log(sorted[1]);  // Should be 1
console.log(sorted[2]);  // Should be 2
console.log(sorted[3]);  // Should be 3

// Test 2: Sort numbers descending
const sortedDesc = sortByDesc(nums, (n: number) => n);
console.log("Sorted descending:");
console.log(sortedDesc[0]);  // Should be 9
console.log(sortedDesc[1]);  // Should be 6
console.log(sortedDesc[2]);  // Should be 5

// Test 3: Verify original array unchanged
console.log("Original unchanged (first element should be 3):");
console.log(nums[0]);

console.log("sortBy tests complete!");
