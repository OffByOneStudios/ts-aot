// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 1
// OUTPUT: true

var m = new Map();
m.set("a", 1);
m.set("b", 2);
console.log(m.get("a"));
console.log(m.has("b"));
