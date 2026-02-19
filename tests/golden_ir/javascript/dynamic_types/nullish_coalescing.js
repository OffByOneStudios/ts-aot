// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: default
// OUTPUT: hello

var a = null;
var b = a ?? "default";
console.log(b);
var c = "hello";
var d = c ?? "default";
console.log(d);
