// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK-NOT: fcmp oeq
// OUTPUT: false

if (1 === "1") {
        console.log("true");
    } else {
        console.log("false");
    }
