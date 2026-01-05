// Test 105.11.5.11: For-in Loop on Object
// Pattern: Iterate object keys

var obj = { a: 1, b: 2, c: 3 };
console.log("Iterating with for-in:");
for (var key in obj) {
    console.log("  key:", key, "value:", obj[key]);
}

// Count keys
var count = 0;
for (var k in obj) {
    count = count + 1;
}
console.log("Key count:", count);

console.log("PASS: for_in");
