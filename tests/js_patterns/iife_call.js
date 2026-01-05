// Test 105.11.5.7: IIFE with .call(this)
// Pattern: Lodash UMD wrapper style

var result = (function() {
    return "hello from call";
}).call(this);
console.log("IIFE .call(this):", result);

// With arguments
var result2 = (function(a, b) {
    return a + b;
}).call(this, 10, 20);
console.log("IIFE .call(this, 10, 20):", result2);

console.log("PASS: iife_call");
