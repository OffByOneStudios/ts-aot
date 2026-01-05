// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_object_set_prop
// OUTPUT: 42

function user_main() {
    var obj = {};
    obj.foo = 42;
    console.log(obj.foo);
    return 0;
}
