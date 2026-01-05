// Debug what lodash receives
const _ = require('lodash');

console.log("=== Testing typeof ===");
console.log("typeof 2:", typeof 2);
console.log("typeof 2.5:", typeof 2.5);
const n: number = 2;
console.log("typeof n:", typeof n);

console.log("\n=== Testing lodash internal functions ===");

// toNumber directly
console.log("_.toNumber(2):", _.toNumber(2));
console.log("_.toNumber(2.5):", _.toNumber(2.5));

// toFinite 
console.log("_.toFinite(2):", _.toFinite(2));

// toInteger is used to convert size parameter
console.log("_.toInteger(2):", _.toInteger(2));

// slice is used to extract chunks  
console.log("_.slice([1,2,3,4], 0, 2):", _.slice([1,2,3,4], 0, 2));

// Now test chunk
console.log("\n=== Testing _.chunk ===");
console.log("_.chunk([1,2,3,4], 2):", _.chunk([1,2,3,4], 2));
