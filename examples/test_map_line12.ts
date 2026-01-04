const m = new Map();
m.set("a", 1);
m.set(2, "b");
m.set(BigInt(3), "c");
const s = Symbol("d");
m.set(s, "e");

console.log("Map size (should be 4):", m.size);
console.log("Map get 'a':", m.get("a"));
console.log("Map get 2:", m.get(2));
console.log("Map get 3n:", m.get(BigInt(3)));
console.log("Map get symbol:", m.get(s));
