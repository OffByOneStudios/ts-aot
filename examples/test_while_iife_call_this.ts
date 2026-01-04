// Test: while loop in IIFE called with `.call(this)`
console.log("Before IIFE");

var result = (function() {
    console.log("IIFE: Start");
    var i = 0;
    var sum = 0;
    while (i < 5) {
        console.log("Loop, i=", i);
        sum = sum + i;
        i = i + 1;
    }
    console.log("IIFE: End, sum=", sum);
    return sum;
}).call(this);

console.log("After IIFE, result=", result);
