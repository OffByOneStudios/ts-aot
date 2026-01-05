// Test 105.11.5.13: Nested Function Definitions
// Pattern: Functions defined inside other functions

function outer() {
    function inner() {
        return 42;
    }
    return inner();
}
console.log("outer() =", outer()); // 42

// Multiple levels
function level1() {
    function level2() {
        function level3() {
            return "deep";
        }
        return level3();
    }
    return level2();
}
console.log("level1() =", level1()); // "deep"

// Inner function accessing outer's params
function makeAdder(x) {
    function add(y) {
        return x + y;
    }
    return add;
}
var add5 = makeAdder(5);
console.log("add5(3) =", add5(3)); // 8

console.log("PASS: nested_functions");
