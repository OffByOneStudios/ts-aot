// Test Set constructor with array
const arr = [1, 2, 3];
console.log("Creating set from array...");

const s = new Set(arr);
console.log("Created. Size:");
console.log(s.size);

console.log("Has 2:");
const has2 = s.has(2);
console.log(has2);

console.log("Has 5:");
const has5 = s.has(5);
console.log(has5);

console.log("Done!");
