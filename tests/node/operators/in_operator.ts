// Test 'in' operator (JavaScript/TypeScript)

function user_main(): number {
    console.log("=== 'in' Operator Test ===");

    // Test 1: Basic 'in' operator check
    console.log("Test 1: Basic in check");
    const obj = { name: "Alice", age: 30 };
    console.log("name" in obj);  // true
    console.log("email" in obj);  // false

    // Test 2: Check with objects
    console.log("Test 2: Object properties");
    const person = {
        name: "Bob",
        greet() { console.log("Hello!"); }
    };
    console.log("name" in person);  // true
    console.log("greet" in person);  // true
    console.log("age" in person);  // false

    // Test 3: Nested objects
    console.log("Test 3: Nested objects");
    const nested = {
        outer: {
            inner: "value"
        }
    };
    console.log("outer" in nested);  // true
    console.log("inner" in nested);  // false (not at top level)

    console.log("=== Tests Complete ===");
    return 0;
}
