// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__ts_module_init
// OUTPUT: 42

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
