// Test: require() with lodash's package.json - proves JSON require works independently
console.log("=== Testing JSON Require with Lodash package.json ===");

const pkg = require("./node_modules/lodash/package.json");

console.log("Lodash name:", pkg.name);
console.log("Lodash version:", pkg.version);
console.log("Lodash license:", pkg.license);

console.log("=== JSON require for lodash package.json works! ===");
