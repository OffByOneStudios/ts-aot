// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: hello world
// OUTPUT: count: 5

var greeting = "hello" + " " + "world";
console.log(greeting);
var count = 5;
console.log("count: " + count);
