// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: ts_object_entries
// OUTPUT: 3

const obj = { a: 1, b: 2, c: 3 };
const entries = Object.entries(obj);
console.log(entries.length);
