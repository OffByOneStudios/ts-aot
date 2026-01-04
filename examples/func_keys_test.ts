// Object.keys on function
function myFunc() { return 1; }
console.log("Before Object.keys");
const keys = Object.keys(myFunc);
console.log("keys length:", keys.length);
console.log("keys:", keys);
console.log("Done");
