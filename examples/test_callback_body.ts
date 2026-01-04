// Test if callback body executes

let callbackWasRun = false;

function callIt(fn: (x: number) => void): void {
    console.log("callIt: calling fn");
    fn(42);
    console.log("callIt: done");
}

callIt(function(x: number) {
    callbackWasRun = true;
    console.log("inside callback, x=" + x);
});

console.log("callbackWasRun: " + callbackWasRun);
