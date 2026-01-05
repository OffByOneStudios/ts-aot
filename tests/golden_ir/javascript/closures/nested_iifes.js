// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__module_init_{{.*}}_any
// OUTPUT: 15

var result = (function() {
        return (function() {
            return (function() {
                return 5 + 10;
            })();
        })();
    })();
    console.log(result);
