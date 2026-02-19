// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: a,b,c

var obj = { a: 1, b: 2, c: 3 };
var keys = Object.keys(obj);
console.log(keys.join(","));
