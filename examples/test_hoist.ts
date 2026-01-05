// Test function hoisting order
const result = (function() {
    function first() {
        return 1;
    }
    
    function second() {
        // second captures first
        return first() + 1;
    }
    
    function third() {
        // third captures second (which captures first)
        return second() + 1;
    }
    
    return third();
})();

console.log("result:", result);  // Should be 3
