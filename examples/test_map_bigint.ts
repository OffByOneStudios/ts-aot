const m = new Map();
m.set("a", 1);
m.set(2, "b");
m.set(BigInt(3), "c");
console.log("Map size (should be 3):", m.size);
