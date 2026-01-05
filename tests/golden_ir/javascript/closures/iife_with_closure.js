// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__module_init_{{.*}}_any
// CHECK: call {{.*}} @ts_cell_create
// OUTPUT: 52

var x = 10;
    var result = (function() { return x + 42; })();
    console.log(result);
