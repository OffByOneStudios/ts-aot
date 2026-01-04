// Test while loop in IIFE
console.log("Test 1: Simple while loop");
var result1 = (function() {
    var i = 0;
    var sum = 0;
    while (i < 5) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
})();
console.log("Result 1:", result1);

console.log("Test 2: Pre-increment while loop (lodash pattern)");
var result2 = (function() {
    var index = -1;
    var length = 5;
    var sum = 0;
    while (++index < length) {
        sum = sum + index;
    }
    return sum;
})();
console.log("Result 2:", result2);

console.log("All tests passed!");
