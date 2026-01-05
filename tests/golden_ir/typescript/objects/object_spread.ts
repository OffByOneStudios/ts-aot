// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: Object spread syntax causes compilation error
// CHECK: define
// CHECK: ts_map_create
// OUTPUT: 3

const obj1 = { a: 1, b: 2 };
const obj2 = { ...obj1, c: 3 };
const keys = Object.keys(obj2);
console.log(keys.length);
