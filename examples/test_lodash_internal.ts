// Test what lodash's internal toFinite does step by step
const _ = require('lodash');

console.log("=== Testing lodash functions ===");

// Check if toFinite exists
console.log("typeof _.toFinite:", typeof _.toFinite);

// Direct calls work
console.log("_.toNumber(2):", _.toNumber(2));

// But toFinite doesn't
console.log("_.toFinite(2):", _.toFinite(2));

// What about calling toFinite directly with a variable?
const val: number = 2;
console.log("val:", val);
console.log("_.toFinite(val):", _.toFinite(val));

// What if we wrap in any?
const valAny: any = 2;
console.log("valAny:", valAny);
console.log("_.toFinite(valAny):", _.toFinite(valAny));
