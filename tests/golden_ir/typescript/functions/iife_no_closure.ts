// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// Test: IIFE (Immediately Invoked Function Expression) without closure
// OUTPUT: 42

const result = (function() { return 42; })();
console.log(result);
