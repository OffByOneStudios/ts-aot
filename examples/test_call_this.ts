// Test .call(this) on IIFE
(function() {
    console.log("Inside IIFE");
}).call(this);
console.log("After IIFE");
