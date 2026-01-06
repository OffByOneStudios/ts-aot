// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__module_init_{{.*}}_any
// CHECK: call {{.*}} @ts_value_sub
// CHECK: call {{.*}} @ts_value_mul
// CHECK: call {{.*}} @ts_value_div
// OUTPUT: 5
// OUTPUT: 50
// OUTPUT: 2.000000

var a = 10;
    var b = 5;
    console.log(a - b);
    console.log(a * b);
    console.log(a / b);
