// Test function hoisting - capturing LATER functions
const result = (function() {
    function first() {
        // first captures second (declared AFTER first)
        return second() + 1;
    }
    
    function second() {
        return 10;
    }
    
    return first();
})();

console.log("result:", result);  // Should be 11
