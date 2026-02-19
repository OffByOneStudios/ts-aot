// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 6
// OUTPUT: true

var str = "hello world";
console.log(str.indexOf("world"));
console.log(str.includes("hello"));
