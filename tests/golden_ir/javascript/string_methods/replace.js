// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: hello universe

var str = "hello world";
var result = str.replace("world", "universe");
console.log(result);
