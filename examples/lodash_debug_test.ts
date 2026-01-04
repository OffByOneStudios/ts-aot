// Test lodash_debug.js with incremental checkpoints
// This file will help us identify which section of lodash causes the hang

console.log("Starting lodash debug test");
const _ = require("./lodash_debug.js");
console.log("Lodash loaded successfully!");
console.log("Type of _:", typeof _);
