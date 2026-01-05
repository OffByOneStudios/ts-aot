// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__ts_module_init
// CHECK: call {{.*}} @ts_value_add
// CHECK-NOT: add i64
// OUTPUT: 30

var a = 10;
    var b = 20;
    var result = a + b;
    console.log(result);
