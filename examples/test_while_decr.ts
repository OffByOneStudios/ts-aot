// Test while(n--) pattern
console.log("Starting while decrement test...");

let count = 10;
let sum = 0;
while (count--) {
    sum = sum + count;
    console.log("count:", count, "sum:", sum);
}

console.log("Final sum:", sum);
console.log("Final count:", count);
