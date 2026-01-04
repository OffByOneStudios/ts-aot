// Test 2: Simple CommonJS IIFE with just console.log
;(function() {
  console.log("Inside IIFE");
  //process.exit(0);
}.call(this));

console.log("After IIFE");
