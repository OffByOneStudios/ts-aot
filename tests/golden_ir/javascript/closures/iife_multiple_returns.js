// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: positive

var x = 10;
    var result = (function() {
        if (x > 0) return "positive";
        if (x < 0) return "negative";
        return "zero";
    })();
    console.log(result);
