// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__ts_module_init
// CHECK: call {{.*}} @ts_value_to_bool
// OUTPUT: truthy

var x = 42;
    if (x) {
        console.log("truthy");
    } else {
        console.log("falsy");
    }
