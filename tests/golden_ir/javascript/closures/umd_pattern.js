// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 42

// UMD-style pattern that returns the factory result
var myLib = (function(global, factory) {
        var result = factory();
        if (typeof module === 'object') {
            module.exports = result;
        } else {
            global.myLib = result;
        }
        return result;  // Return the result so it can be assigned
    })(this, function() {
        return { version: 42 };
    });
    console.log(myLib.version);
