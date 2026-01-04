const m = new Map();
console.log("Created map");
m.set("a", 1);
console.log("Set a=1, size:", m.size);
const s = Symbol("d");
console.log("Created symbol:", s);
m.set(s, "e");
console.log("Set symbol=e, size:", m.size);
console.log("All done!");
