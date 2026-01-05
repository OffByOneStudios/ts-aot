// Test: require() with JSON file
console.log("Starting JSON require test...");

// Try to require a simple JSON file
const pkg = require("./test_data.json");

console.log("JSON loaded successfully!");
console.log("Name:", pkg.name);
console.log("Version:", pkg.version);
console.log("Done!");
