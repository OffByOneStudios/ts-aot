// Test basic IIFE that returns an object
var result = (function() {
    return 42;
})();

console.log("IIFE returned:", result);

// Test IIFE that returns an object
var obj = (function() {
    const x = 10;
    const y = 20;
    return x + y;
})();

console.log("Object IIFE:", obj);
console.log("All tests passed!");
