// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: ts_gc_alloc
// OUTPUT: 3

const obj1 = { a: 1 };
const obj2 = { b: 2 };
const result = Object.assign(obj1, obj2, { c: 3 });
const keys = Object.keys(result);
console.log(keys.length);
