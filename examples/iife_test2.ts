// Test 2: Function expression call
console.log("Before");
var result = (function() {
    console.log("Inside function");
    return 42;
})();
console.log("Result:", result);
console.log("After");
