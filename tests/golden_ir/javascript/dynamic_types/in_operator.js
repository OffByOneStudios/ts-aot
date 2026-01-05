// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// OUTPUT: true

function user_main() {
    var obj = { a: 1, b: 2 };
    console.log('a' in obj);
    return 0;
}
