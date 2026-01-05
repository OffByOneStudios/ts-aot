// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// OUTPUT: 3

function user_main() {
    var obj = { a: 1, b: 2, c: 3 };
    console.log(Object.values(obj).length);
    return 0;
}
