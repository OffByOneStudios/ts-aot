// Debug test to see array access issue
console.log("=== Debug JSON Array Access ===");

const data = require("./test_data_full.json");

// Check if it's actually an array
const tags = data.tags;
console.log("typeof tags:", typeof tags);
console.log("tags:", tags);

// Now try accessing elements
console.log("tags[0]:", tags[0]);
console.log("tags[1]:", tags[1]);
console.log("tags[2]:", tags[2]);

console.log("=== Done ===");
