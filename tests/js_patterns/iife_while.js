// Test 105.11.5.10: While Loop in IIFE
// Pattern: IIFE containing while loop (suspected issue)

var result = (function() {
    var i = 0;
    while (i < 10) {
        i = i + 1;
    }
    return i;
})();
console.log("While loop result:", result); // 10

// With break
var result2 = (function() {
    var i = 0;
    while (true) {
        i = i + 1;
        if (i >= 5) {
            break;
        }
    }
    return i;
})();
console.log("While with break:", result2); // 5

console.log("PASS: iife_while");
