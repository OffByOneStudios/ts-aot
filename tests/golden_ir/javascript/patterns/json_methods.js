// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: Alice
// OUTPUT: 30

// Test JSON.parse
var json = '{"name":"Alice","age":30}';
var obj = JSON.parse(json);
console.log(obj.name);
console.log(obj.age);
