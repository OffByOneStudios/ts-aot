// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: yes
// OUTPUT: no

var x = 10;
console.log(x > 5 ? "yes" : "no");
console.log(x > 20 ? "yes" : "no");
