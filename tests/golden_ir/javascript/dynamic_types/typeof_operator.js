// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_typeof
// OUTPUT: number
// OUTPUT: string

var x = 42;
var y = "hello";
console.log(typeof x);
console.log(typeof y);
