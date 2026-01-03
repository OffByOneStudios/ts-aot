// Test IIFE calls
console.log("Before IIFE");

(function() {
    console.log("Inside IIFE");
    var x = 42;
    console.log("x =", x);
}).call(this);

console.log("After IIFE");
