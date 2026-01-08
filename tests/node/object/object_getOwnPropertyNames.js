// Test Object.getOwnPropertyNames

// Test 1: Basic object
const obj = { a: 1, b: 2 };
const names = Object.getOwnPropertyNames(obj);
console.log(names.length);  // Expected: 2

// Test 2: Print the property names (order may vary)
for (let i = 0; i < names.length; i++) {
    console.log(names[i]);  // Expected: "a" and "b"
}

// Test 3: Empty object
const empty = {};
const emptyNames = Object.getOwnPropertyNames(empty);
console.log(emptyNames.length);  // Expected: 0
