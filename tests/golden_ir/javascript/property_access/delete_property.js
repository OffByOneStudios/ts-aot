// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: true

var obj = { foo: 42 };
    delete obj.foo;
    console.log(obj.foo === undefined);
