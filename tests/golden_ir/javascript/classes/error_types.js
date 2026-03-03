// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// Test: Error construction (JS slow path)
// CHECK: define
// OUTPUT: hello
// OUTPUT: false
// OUTPUT: type error
// OUTPUT: false
// NOTE: instanceof returns false because Error/TypeError are not in the
//       class registry. Error construction works but prototype chain is incomplete.

var err = new Error("hello");
console.log(err.message);
console.log(err instanceof Error);

var te = new TypeError("type error");
console.log(te.message);
console.log(te instanceof TypeError);
