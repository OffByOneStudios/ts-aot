// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__ts_module_init
// OUTPUT: 42
// OUTPUT: secret

var myModule = (function() {
        var privateVar = "secret";
        return {
            publicMethod: function() { return privateVar; },
            publicVar: 42
        };
    })();
    console.log(myModule.publicVar);
    console.log(myModule.publicMethod());
