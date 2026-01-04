// Test: typeof function assigned to any variable

function myFunc() { return 1; }

// Direct typeof - should work
console.log("Direct typeof: " + typeof myFunc);

// Assign to any variable - test this case
let fn: any = myFunc;
console.log("typeof any var: " + typeof fn);

// What's the actual value?
console.log("fn value:", fn);

console.log("Done");
