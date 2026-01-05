// Nested function test
const result = (function() {
    function outer() {
        function inner() {
            return 42;
        }
        return inner();
    }
    return outer();
})();

console.log("result:", result);  // Should be 42
