// Test multi-declarator var inside function

(function() {
    console.log("IIFE start");
    
    var a = 1, b = 2, c = 3;
    console.log("After multi-var, a=", a, "b=", b, "c=", c);
    
    var x = "hello", y = "world", z = "!";
    console.log("After second multi-var, x=", x, "y=", y, "z=", z);
}).call(this);

console.log("Done");
