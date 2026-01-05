// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__ts_module_init
// CHECK: call {{.*}} @ts_value_strict_eq
// OUTPUT: false

if (1 === "1") {
        console.log("true");
    } else {
        console.log("false");
    }
