// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_object_get_dynamic
// OUTPUT: 42

function user_main() {
    var obj = { foo: 42, bar: 100 };
    var key = "foo";
    var val = obj[key];
    console.log(val);
    return 0;
}
