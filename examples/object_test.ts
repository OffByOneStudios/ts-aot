// Test Object static methods

const obj = { a: 1, b: 2, c: 3 };

// Test Object.keys
console.log("Object.keys test:");
const keys = Object.keys(obj);
console.log("keys length:", keys.length);

// Test Object.values
console.log("\nObject.values test:");
const vals = Object.values(obj);
console.log("values length:", vals.length);

// Test Object.entries
console.log("\nObject.entries test:");
const ents = Object.entries(obj);
console.log("entries length:", ents.length);

// Test Object.assign
console.log("\nObject.assign test:");
const tgt = { x: 1 };
const src = { y: 2 };
Object.assign(tgt, src);
const tgtKeys = Object.keys(tgt);
console.log("target keys count:", tgtKeys.length);

// Test Object.hasOwn
console.log("\nObject.hasOwn test:");
const has1 = Object.hasOwn(obj, "a");
console.log("hasOwn 'a':", has1);
const has2 = Object.hasOwn(obj, "z");
console.log("hasOwn 'z':", has2);

console.log("\nAll Object tests passed!");
