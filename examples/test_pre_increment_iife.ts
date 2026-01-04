// Test pre-increment in while loop inside IIFE
console.log("Testing pre-increment");
var result = (function() {
    console.log("IIFE start");
    var index = -1;
    var length = 5;
    var sum = 0;
    console.log("Before while, index=", index);
    while (++index < length) {
        console.log("In loop, index=", index, "sum=", sum);
        sum = sum + index;
    }
    console.log("After while, sum=", sum);
    return sum;
})();
console.log("Result:", result);
