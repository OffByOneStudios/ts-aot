// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: hello
// OUTPUT: undefined

var obj = { name: "hello" };
console.log(obj?.name);
var empty = null;
console.log(empty?.name);
