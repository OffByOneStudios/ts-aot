// Test CommonJS require behavior
const cjs = require("./cjs_test.js");

console.log("main: typeof cjs:", typeof cjs);
console.log("main: cjs.hello:", cjs.hello);
