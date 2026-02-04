// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: fcmp oeq
// OUTPUT: true

if (1 == "1") {
        console.log("true");
    } else {
        console.log("false");
    }
