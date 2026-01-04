// Test named vs anonymous callback

let ran1 = false;
let ran2 = false;

function myNamedCallback(x: number): void {
    ran1 = true;
    console.log("named callback: " + x);
}

function callIt(fn: (x: number) => void): void {
    fn(42);
}

console.log("Testing named function...");
callIt(myNamedCallback);
console.log("ran1: " + ran1);

console.log("Testing anonymous function...");
callIt(function(x: number) {
    ran2 = true;
    console.log("anon callback: " + x);
});
console.log("ran2: " + ran2);
