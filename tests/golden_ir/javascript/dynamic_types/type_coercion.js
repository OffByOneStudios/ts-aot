// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_string_concat
// OUTPUT: 42string

var result = 42 + "string";
    console.log(result);
