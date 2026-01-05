// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// OUTPUT: 15

function user_main() {
    var result = (function() {
        return (function() {
            return (function() {
                return 5 + 10;
            })();
        })();
    })();
    console.log(result);
    return 0;
}
