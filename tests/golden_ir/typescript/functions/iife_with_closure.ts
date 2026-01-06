// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// Test: IIFE (Immediately Invoked Function Expression) with closure
// OUTPUT: 52

const x = 10;
const result = (function() { return x + 42; })();
console.log(result);
