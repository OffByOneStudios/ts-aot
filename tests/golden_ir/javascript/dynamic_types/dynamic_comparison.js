// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__module_init_{{.*}}_any
// CHECK: call {{.*}} @ts_value_lt
// CHECK: call {{.*}} @ts_value_gt
// OUTPUT: true
// OUTPUT: false

var a = 5;
    var b = 10;
    console.log(a < b);
    console.log(a > b);
