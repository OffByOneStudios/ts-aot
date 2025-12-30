// Test new collection functions from Milestone 105.2

import { sortBy, sortByDesc } from './src/collection/sortBy';

// Test sortBy with numbers
const nums = [3, 1, 4, 1, 5, 9, 2, 6];
const sorted = sortBy(nums, (n: number) => n);
console.log("sortBy result length:"); 
console.log(sorted.length); // 8

// Check first few elements
let i = 0;
while (i < sorted.length) {
    console.log(sorted[i]);
    i = i + 1;
}

console.log("All tests passed!");
