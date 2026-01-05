// Test 105.11.5.4: Basic Function Definition & Call
// Pattern: Function declaration and invocation

function add(a, b) {
    return a + b;
}

var result = add(2, 3);
console.log("add(2, 3) =", result);

function greet(name) {
    return "Hello, " + name + "!";
}

console.log(greet("World"));
console.log("PASS: basic_function");
