// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// OUTPUT: true

function user_main() {
    var obj = { foo: 42 };
    delete obj.foo;
    console.log(obj.foo === undefined);
    return 0;
}
