// Debug callback test

function myCallback(x: number): void {
    console.log("myCallback: " + x);
}

function callIt(fn: (x: number) => void): void {
    console.log("callIt: fn is " + typeof fn);
    if (fn) {
        console.log("callIt: calling fn(42)");
        fn(42);
        console.log("callIt: called");
    } else {
        console.log("callIt: fn is null/undefined");
    }
}

console.log("Test 1: named function");
callIt(myCallback);

console.log("Test 2: anonymous function");
callIt(function(x: number) {
    console.log("anon callback: " + x);
});
