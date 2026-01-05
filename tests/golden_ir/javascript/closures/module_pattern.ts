// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// OUTPUT: 42
// OUTPUT: secret

function user_main() {
    var myModule = (function() {
        var privateVar = "secret";
        return {
            publicMethod: function() { return privateVar; },
            publicVar: 42
        };
    })();
    console.log(myModule.publicVar);
    console.log(myModule.publicMethod());
    return 0;
}
