// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: fcmp olt
// CHECK: fcmp ogt
// OUTPUT: true
// OUTPUT: false

var a = 5;
    var b = 10;
    console.log(a < b);
    console.log(a > b);
