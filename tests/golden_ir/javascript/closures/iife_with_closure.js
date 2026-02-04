// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 52

var x = 10;
    var result = (function() { return x + 42; })();
    console.log(result);
