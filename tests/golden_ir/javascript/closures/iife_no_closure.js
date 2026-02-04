// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK-NOT: ts_cell_create
// OUTPUT: 42

var result = (function() { return 42; })();
    console.log(result);
