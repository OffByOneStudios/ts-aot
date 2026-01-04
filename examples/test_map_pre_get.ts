const m = new Map();
m.set("a", 1);
m.set(2, "b");
m.set(BigInt(3), "c");
const s = Symbol("d");
m.set(s, "e");

console.log("Map size (should be 4):", m.size);
console.log("About to call m.get...");
