// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 3

var obj = { a: { b: { c: 3 } } };
console.log(obj.a.b.c);
