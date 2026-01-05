// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// OUTPUT: 3

function user_main() {
    var obj = { a: { b: { c: 3 } } };
    console.log(obj.a.b.c);
    return 0;
}
