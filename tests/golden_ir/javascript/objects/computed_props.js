// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: hello

var key = "greeting";
var obj = {};
obj[key] = "hello";
console.log(obj[key]);
