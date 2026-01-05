// Test calling from outside the defining scope
const result = (function() {
    function second() {
        return 10;
    }
    
    function first() {
        // first captures second (declared AFTER in hoisting)
        return second() + 1;
    }
    
    // Return the function, not call it
    return first;
})();

// Call the returned function from OUTSIDE the IIFE
console.log("result:", result());  // Should be 11
