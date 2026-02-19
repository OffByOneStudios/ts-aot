// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: hello Alice, age 30

var name = "Alice";
var age = 30;
console.log("hello " + name + ", age " + age);
