// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__ts_module_init
// CHECK: call {{.*}} @ts_value_sub
// CHECK: call {{.*}} @ts_value_mul
// CHECK: call {{.*}} @ts_value_div
// OUTPUT: 5
// OUTPUT: 50
// OUTPUT: 2

var a = 10;
    var b = 5;
    console.log(a - b);
    console.log(a * b);
    console.log(a / b);
