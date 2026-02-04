// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 42

var obj = { value: 42 };
    var result = (function() { return this.value; }).call(obj);
    console.log(result);
