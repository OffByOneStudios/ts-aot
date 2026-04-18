// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// Test: Error construction (JS slow path)
// CHECK: define
// OUTPUT: hello
// OUTPUT: true
// OUTPUT: type error
// OUTPUT: true

var err = new Error("hello");
console.log(err.message);
console.log(err instanceof Error);

var te = new TypeError("type error");
console.log(te.message);
console.log(te instanceof TypeError);
