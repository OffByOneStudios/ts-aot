// Simplest typeof test

function f() { return 1; }

const obj: any = {
    method: f
};

console.log("Test 1: Direct function");
console.log("typeof f = '" + typeof f + "'");

console.log("\nTest 2: Direct property access");
console.log("typeof obj.method = '" + typeof obj.method + "'");

console.log("\nTest 3: Computed property access");
const key = "method";
const value = obj[key];
console.log("value = " + value);
console.log("typeof value = '" + typeof value + "'");

console.log("\nDone");
