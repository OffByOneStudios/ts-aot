// Test typeof operator and lodash number functions

const _ = require('./lodash_full.js');

console.log("=== typeof Operator Test ===");

// Test with statically typed values
let numVal = 42;
console.log("typeof 42:", typeof numVal);
console.log("typeof numVal == 'number':", typeof numVal == 'number');

// Test with lodash functions
console.log("\n=== Lodash toNumber test ===");
const toNumResult = _.toNumber(2);
console.log("_.toNumber(2):", toNumResult);

console.log("\n=== Lodash toFinite test ===");
const toFinResult = _.toFinite(2);
console.log("_.toFinite(2):", toFinResult);

console.log("\n=== Lodash toInteger test ===");
const toIntResult = _.toInteger(2);
console.log("_.toInteger(2):", toIntResult);
console.log(typeof boolVal);

// Test with any type (uses runtime ts_typeof)
let anyNum: any = 123;
console.log("typeof (any)123:");
console.log(typeof anyNum);

let anyStr: any = "world";
console.log("typeof (any)'world':");
console.log(typeof anyStr);

let anyObj: any = { a: 1 };
console.log("typeof (any){a:1}:");
console.log(typeof anyObj);

let anyArr: any = [1, 2, 3];
console.log("typeof (any)[1,2,3]:");
console.log(typeof anyArr);

let anyUndef: any = undefined;
console.log("typeof (any)undefined:");
console.log(typeof anyUndef);

console.log("");
console.log("=== typeof Test Complete ===");
