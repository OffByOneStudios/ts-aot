// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__module_init_{{.*}}_any
// CHECK: call {{.*}} @ts_call_0
// OUTPUT: 1

var result = (function() { return { a: 1 }; })();
    console.log(result.a);
