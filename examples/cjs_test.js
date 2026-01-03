// Simple CommonJS module test
console.log("cjs_test.js: start");

// Check module and exports
console.log("typeof exports:", typeof exports);
console.log("typeof module:", typeof module);

// Test if we can assign to module.exports
var myValue = { hello: "world" };
module.exports = myValue;

console.log("cjs_test.js: done");
