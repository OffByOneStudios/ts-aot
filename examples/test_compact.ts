// Test compact() function with truthiness

import { compact } from "./ts-lodash/src/array/compact";

console.log("=== compact() Test ===");

// Test 1: Mixed array with falsy values
let arr1: any[] = [0, 1, false, 2, "", 3];
console.log("Input: [0, 1, false, 2, '', 3]");
let result1 = compact(arr1);
console.log("compact result:");
let i = 0;
for (i = 0; i < result1.length; i++) {
    console.log(result1[i]);
}
// Expected: [1, 2, 3]

console.log("");

// Test 2: Array with nulls and undefined
let arr2: any[] = [null, undefined, 1, 2, null];
console.log("Input: [null, undefined, 1, 2, null]");
let result2 = compact(arr2);
console.log("compact result:");
for (i = 0; i < result2.length; i++) {
    console.log(result2[i]);
}
// Expected: [1, 2]

console.log("");

// Test 3: All truthy values
let arr3: any[] = [1, "hello", true, 42];
console.log("Input: [1, 'hello', true, 42]");
let result3 = compact(arr3);
console.log("compact result:");
for (i = 0; i < result3.length; i++) {
    console.log(result3[i]);
}
// Expected: [1, "hello", true, 42]

console.log("");

// Test 4: All falsy values
let arr4: any[] = [0, "", false, null, undefined];
console.log("Input: [0, '', false, null, undefined]");
let result4 = compact(arr4);
console.log("compact result length:");
console.log(result4.length);
// Expected: 0 (empty array)

console.log("");
console.log("=== All compact() tests complete ===");
