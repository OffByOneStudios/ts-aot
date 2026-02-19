// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: true
// OUTPUT: false

var pattern = /hello/;
console.log(pattern.test("hello world"));
console.log(pattern.test("goodbye"));
