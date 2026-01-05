// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__ts_module_init
// CHECK: call {{.*}} @ts_call_0
// OUTPUT: 1

var result = (function() { return { a: 1 }; })();
    console.log(result.a);
