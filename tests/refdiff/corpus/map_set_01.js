var m = new Map();
m.set("a", 1);
m.set("b", 2);
m.set("c", 3);
console.log("size:", m.size);
console.log("get a:", m.get("a"));
console.log("has b:", m.has("b"));
console.log("has z:", m.has("z"));
m.delete("b");
console.log("after delete size:", m.size);

var keys = [];
m.forEach((v, k) => keys.push(k + "=" + v));
console.log(keys.sort().join(","));

var s = new Set([1, 2, 3, 2, 1]);
console.log("set size:", s.size);
console.log("has 1:", s.has(1));
console.log("has 4:", s.has(4));
s.add(99);
console.log("after add 99 size:", s.size);
