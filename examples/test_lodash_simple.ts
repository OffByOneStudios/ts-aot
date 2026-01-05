// Simple require test
const _ = require('lodash');

console.log("typeof _:", typeof _);
console.log("typeof _.identity:", typeof _.identity);

// Test a simple function call
const x = _.identity(42);
console.log("_.identity(42):", x);
