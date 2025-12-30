// Simple Set test
const seen = new Set<number>();
console.log("Set created");

seen.add(1);
console.log("Added 1");

seen.add(2);
console.log("Added 2");

const hasOne = seen.has(1);
console.log("Has 1:");
console.log(hasOne);

console.log("Done!");
