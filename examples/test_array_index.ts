// Test array indexing issue

console.log("=== Array Indexing Test ===\n");

// Test array indexing
console.log("1. Direct array creation:");
const arr = ["a", "b", "c"];
console.log("  arr.length:", arr.length);
console.log("  arr[0]:", arr[0]);
console.log("  arr[1]:", arr[1]);
console.log("  arr[2]:", arr[2]);

// Test split result
console.log("\n2. String split:");
const testPath = "user.profile.name";
const parts = testPath.split(".");
console.log("  parts.length:", parts.length);
console.log("  parts[0]:", parts[0]);
console.log("  parts[1]:", parts[1]);
console.log("  parts[2]:", parts[2]);

// Test with loop
console.log("\n3. Loop through parts:");
for (let i = 0; i < parts.length; i = i + 1) {
    const part = parts[i];
    console.log("    i=", i, "part=", part);
}

console.log("\n=== Done ===");
