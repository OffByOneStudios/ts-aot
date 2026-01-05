// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// OUTPUT: positive

function user_main() {
    var x = 10;
    var result = (function() {
        if (x > 0) return "positive";
        if (x < 0) return "negative";
        return "zero";
    })();
    console.log(result);
    return 0;
}
