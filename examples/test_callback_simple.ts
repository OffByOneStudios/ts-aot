// Simple callback test

function callIt(fn: (x: number) => void): void {
    console.log("callIt: before");
    fn(42);
    console.log("callIt: after");
}

console.log("Step 1");
callIt(function(x: number) {
    console.log("callback got: " + x);
});
console.log("Step 2");
