// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// OUTPUT: 42

function user_main() {
    var myLib = (function(global, factory) {
        if (typeof module === 'object') {
            module.exports = factory();
        } else {
            global.myLib = factory();
        }
    })(this, function() {
        return { version: 42 };
    });
    console.log(myLib.version);
    return 0;
}
