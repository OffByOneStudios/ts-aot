// Test while loop NOT in IIFE
console.log("Test: While loop outside IIFE");
var i = 0;
var sum = 0;
while (i < 5) {
    sum = sum + i;
    i = i + 1;
}
console.log("Sum:", sum);

console.log("Test 2: Pre-increment");
var index = -1;
var length = 5;
var sum2 = 0;
while (++index < length) {
    sum2 = sum2 + index;
}
console.log("Sum2:", sum2);

console.log("Done!");
