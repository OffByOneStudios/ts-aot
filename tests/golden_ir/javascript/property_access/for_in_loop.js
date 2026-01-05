// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// OUTPUT: a
// OUTPUT: b
// OUTPUT: c

function user_main() {
    var obj = { a: 1, b: 2, c: 3 };
    for (var key in obj) {
        console.log(key);
    }
    return 0;
}
