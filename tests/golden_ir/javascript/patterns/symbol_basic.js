// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: symbol
// OUTPUT: true

// Test basic Symbol usage
var s1 = Symbol("mySymbol");
console.log(typeof s1);

// Symbols are unique
console.log(String(Symbol("a") !== Symbol("a")));
