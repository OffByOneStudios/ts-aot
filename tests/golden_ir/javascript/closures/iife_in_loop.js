// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 0
// OUTPUT: 1
// OUTPUT: 2

var i = 0;
    while (i < 3) {
        (function(n) {
            console.log(n);
        })(i);
        i++;
    }
