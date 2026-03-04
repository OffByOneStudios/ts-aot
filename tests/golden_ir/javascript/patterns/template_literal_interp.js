// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: Hello Alice, you are 30 years old
// OUTPUT: 2 + 3 = 5
// OUTPUT: multi
// OUTPUT: line

var name = "Alice";
var age = 30;
console.log(`Hello ${name}, you are ${age} years old`);
console.log(`${2} + ${3} = ${2 + 3}`);
console.log(`multi
line`);
