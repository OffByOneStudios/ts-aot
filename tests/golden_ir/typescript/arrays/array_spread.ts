// Test: Array spread
const arr1 = [1, 2];
const arr2 = [3, 4];
const combined = [...arr1, ...arr2];
console.log(combined.length);

// CHECK: ts_array_create_specialized
// CHECK: ts_array_concat

// OUTPUT: 4
