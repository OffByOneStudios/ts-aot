// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: fadd
// OUTPUT: 30

var a = 10;
    var b = 20;
    var result = a + b;
    console.log(result);
