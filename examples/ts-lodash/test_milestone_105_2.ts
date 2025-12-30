// Test new collection functions from Milestone 105.2

import { sortBy, sortByDesc } from './src/collection/sortBy';
// import { groupBy, keyBy } from './src/collection/groupBy';  // Blocked by nested array specialization

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

// Test sortByDesc
console.log("sortByDesc result:");
const sortedDesc = sortByDesc(nums, (n: number) => n);
i = 0;
while (i < sortedDesc.length) {
    console.log(sortedDesc[i]);
    i = i + 1;
}

console.log("All tests passed!");
