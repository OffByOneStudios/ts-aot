// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: hello
// OUTPUT: world
// OUTPUT: hello-world

var str = "hello world";
var parts = str.split(" ");
console.log(parts[0]);
console.log(parts[1]);
console.log(parts.join("-"));
