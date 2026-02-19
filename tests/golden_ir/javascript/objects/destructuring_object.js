// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: Alice
// OUTPUT: 30

var person = { name: "Alice", age: 30 };
var name = person.name;
var age = person.age;
console.log(name);
console.log(age);
