// Test 105.11.5.20: Large Object with 50+ Properties
// Pattern: Tests memory allocation with many properties

var obj = {};
for (var i = 0; i < 50; i++) {
    obj['prop' + i] = i;
}

console.log("Object created with 50 properties");
console.log("prop0:", obj.prop0);
console.log("prop25:", obj.prop25);
console.log("prop49:", obj.prop49);

// Verify all properties
var count = 0;
for (var key in obj) {
    count = count + 1;
}
console.log("Property count:", count);

console.log("PASS: large_object");
