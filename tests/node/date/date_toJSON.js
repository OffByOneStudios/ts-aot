// Test Date.prototype.toJSON

// Create a specific date for consistent testing
const date = new Date(1705322400000);  // 2024-01-15T14:00:00.000Z

// toJSON should return ISO string format
const json = date.toJSON();
console.log(json);  // Expected: 2024-01-15T14:00:00.000Z

// toJSON and toISOString should return the same value
const iso = date.toISOString();
console.log(iso);  // Expected: 2024-01-15T14:00:00.000Z

// Verify they match
if (json === iso) {
    console.log("MATCH");
} else {
    console.log("MISMATCH");
}
