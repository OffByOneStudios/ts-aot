// Minimal test case for cell-based hoisting
const result = (function() {
    function helper() {
        return 42;
    }
    return helper();
})();

console.log("result:", result);  // Should be 42
