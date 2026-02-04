// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: 1

var result = (function() { return { a: 1 }; })();
    console.log(result.a);
