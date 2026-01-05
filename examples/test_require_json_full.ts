// Comprehensive test: require() with JSON file - all types
console.log("=== JSON Require Full Test ===");

const data = require("./test_data_full.json");

// Test string
console.log("String (name):", data.name);

// Test number
console.log("Number (version):", data.version);

// Test boolean
console.log("Boolean (enabled):", data.enabled);

// Test null
console.log("Null (nullable):", data.nullable);

// Test nested object
console.log("Nested object (settings.debug):", data.settings.debug);
console.log("Nested object (settings.timeout):", data.settings.timeout);

// Test array
console.log("Array (tags[0]):", data.tags[0]);
console.log("Array (tags[1]):", data.tags[1]);
console.log("Array length:", data.tags.length);

console.log("=== All tests complete ===");
