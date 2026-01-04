// Test IIFE with .call(this)
console.log("Before IIFE");
var result = (function() {
    console.log("Inside IIFE");
    return 42;
}).call(this);
console.log("After IIFE, result:", result);
