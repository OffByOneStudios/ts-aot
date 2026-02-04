// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: fsub
// CHECK: fmul
// CHECK: fdiv
// OUTPUT: 5
// OUTPUT: 50
// OUTPUT: 2

var a = 10;
    var b = 5;
    console.log(a - b);
    console.log(a * b);
    console.log(a / b);
