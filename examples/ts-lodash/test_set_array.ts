// Set + Array access test
const arr = [1, 2, 1, 3, 2, 4];
console.log("Array created");

const seen = new Set<number>();
console.log("Set created");

// Access first element
const first = arr[0];
console.log("First element:");
console.log(first);

// Add to set
seen.add(first);
console.log("Added to set");

// Check has
const hasFirst = seen.has(first);
console.log("Has first:");
console.log(hasFirst);

console.log("Done!");
