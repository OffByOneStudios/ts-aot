// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: HELLO
// OUTPUT: hello

var str = "Hello";
console.log(str.toUpperCase());
console.log(str.toLowerCase());
