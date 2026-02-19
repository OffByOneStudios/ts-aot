// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: hello

var str = "  hello  ";
console.log(str.trim());
