// Test: Ternary operator conditional expression
const x = 10;
const y = 5;
const max = x > y ? x : y;
console.log(max);

const min = x < y ? x : y;
console.log(min);

// CHECK: fcmp
// CHECK: select

// OUTPUT: 10
// OUTPUT: 5
