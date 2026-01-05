// Test 105.11.5.6: Simple IIFE (Immediately Invoked Function Expression)
// Pattern: Basic IIFE without .call()

var result1 = (function() {
    return 42;
})();
console.log("IIFE result:", result1);

var result2 = (function(x) {
    return x * 2;
})(21);
console.log("IIFE with arg:", result2);

console.log("PASS: iife_simple");
