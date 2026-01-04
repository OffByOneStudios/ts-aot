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
console.log("Map has 'a':", m.has("a"));
console.log("Map has 'x':", m.has("x"));

m.delete("a");
console.log("Map has 'a' after delete:", m.has("a"));

m.clear();
console.log("Map size after clear (should be 0):", m.size);

const set = new Set();
set.add(1);
set.add("2");
set.add(BigInt(3));
set.add(s);

console.log("Set size (should be 4):", set.size);
console.log("Set has 1:", set.has(1));
console.log("Set has '2':", set.has("2"));
console.log("Set has 3n:", set.has(BigInt(3)));
console.log("Set has symbol:", set.has(s));
console.log("Set has 4:", set.has(4));

set.delete(1);
console.log("Set size after delete (should be 3):", set.size);
console.log("Set has 1 after delete:", set.has(1));

set.clear();
console.log("Set size after clear (should be 0):", set.size);
console.log("Set has '2' after clear:", set.has("2"));
