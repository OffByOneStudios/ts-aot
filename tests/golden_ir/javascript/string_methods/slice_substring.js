// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: world
// OUTPUT: hello

var str = "hello world";
console.log(str.slice(6));
console.log(str.substring(0, 5));
