// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// Test: Object destructuring
// OUTPUT: 1
// OUTPUT: 2

const obj = { a: 1, b: 2, c: 3 };
const { a, b } = obj;
console.log(a);
console.log(b);
