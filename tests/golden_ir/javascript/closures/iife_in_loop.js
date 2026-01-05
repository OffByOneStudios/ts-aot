// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// OUTPUT: 0
// OUTPUT: 1
// OUTPUT: 2

function user_main() {
    var i = 0;
    while (i < 3) {
        (function(n) {
            console.log(n);
        })(i);
        i++;
    }
    return 0;
}
