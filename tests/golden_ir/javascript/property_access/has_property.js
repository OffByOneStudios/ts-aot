// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// OUTPUT: true
// OUTPUT: false

function user_main() {
    var obj = { foo: 42 };
    console.log(obj.hasOwnProperty('foo'));
    console.log(obj.hasOwnProperty('bar'));
    return 0;
}
