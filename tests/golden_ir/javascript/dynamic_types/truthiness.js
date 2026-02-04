// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: fcmp one
// OUTPUT: truthy

var x = 42;
    if (x) {
        console.log("truthy");
    } else {
        console.log("falsy");
    }
