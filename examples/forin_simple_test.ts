// Simple for-in test
const obj = { a: 1, b: 2 };
console.log("Before for-in");
for (var k in obj) {
    console.log("Key:", k);
}
console.log("After for-in");
