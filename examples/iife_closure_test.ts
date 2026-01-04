// Test IIFE with closure and variable assignment
const result = (function() {
    let value = 10;
    return function() {
        value = 20; // Assignment to closure variable
        return value;
    };
})();

console.log("Result:", result());
