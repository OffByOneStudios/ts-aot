// Test 105.11.5.3: Array Operations
// Pattern: Array creation, push, length, index access, for loop

var arr = [1, 2, 3];
arr.push(4);
console.log("length:", arr.length);
console.log("arr[0]:", arr[0]);
console.log("arr[3]:", arr[3]);

console.log("Iterating with for loop:");
for (var i = 0; i < arr.length; i++) {
    console.log("  arr[" + i + "]:", arr[i]);
}
console.log("PASS: array_ops");
