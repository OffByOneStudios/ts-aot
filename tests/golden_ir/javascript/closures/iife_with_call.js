// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// OUTPUT: 42

function user_main() {
    var obj = { value: 42 };
    var result = (function() { return this.value; }).call(obj);
    console.log(result);
    return 0;
}
