// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: Object.entries() returns array with length 0
// CHECK: define
// CHECK: ts_map_create
// OUTPUT: 3

const obj = { a: 1, b: 2, c: 3 };
const entries = Object.entries(obj);
console.log(entries.length);
