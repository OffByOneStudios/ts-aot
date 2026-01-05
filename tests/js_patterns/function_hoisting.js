// Test 105.11.5.14: Function Hoisting
// Pattern: Call function before its declaration

console.log("hoisted() =", hoisted()); // "works"

function hoisted() {
    return "works";
}

// Multiple hoisted functions
console.log("second() =", second());  // "also works"
console.log("first() =", first());    // "first"

function first() { return "first"; }
function second() { return "also works"; }

console.log("PASS: function_hoisting");
