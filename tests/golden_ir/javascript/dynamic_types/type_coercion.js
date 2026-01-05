// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__ts_module_init
// CHECK: call {{.*}} @ts_value_add
// OUTPUT: 42string

var result = 42 + "string";
    console.log(result);
