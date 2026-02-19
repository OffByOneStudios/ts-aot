// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 15
// OUTPUT: 30
// OUTPUT: 10
// OUTPUT: 3

var x = 20;
x -= 5;
console.log(x);
x *= 2;
console.log(x);
x /= 3;
console.log(x);
x %= 7;
console.log(x);
