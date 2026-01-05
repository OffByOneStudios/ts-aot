// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__module_init_{{.*}}_any
// CHECK: call {{.*}} @ts_value_strict_eq
// OUTPUT: false

if (1 === "1") {
        console.log("true");
    } else {
        console.log("false");
    }
