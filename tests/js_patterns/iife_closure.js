// Test 105.11.5.8: IIFE with Closure
// Pattern: Counter pattern using closure

var counter = (function() {
    var count = 0;
    return function() {
        count = count + 1;
        return count;
    };
})();

console.log("counter() =", counter()); // 1
console.log("counter() =", counter()); // 2
console.log("counter() =", counter()); // 3

// Multiple independent counters
var counter2 = (function() {
    var count = 100;
    return function() {
        count = count + 1;
        return count;
    };
})();

console.log("counter2() =", counter2()); // 101
console.log("counter() =", counter());   // 4 (original still works)

console.log("PASS: iife_closure");
