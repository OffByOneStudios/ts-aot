// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_object_get_prop
// OUTPUT: 42

function user_main() {
    var obj = { foo: 42 };
    var val = obj.foo;
    console.log(val);
    return 0;
}
