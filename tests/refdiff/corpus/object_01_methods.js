var o = {a: 1, b: 2, c: 3};
console.log("keys:", Object.keys(o).sort().join(","));
console.log("values:", Object.values(o).sort().join(","));
console.log("entries:", JSON.stringify(Object.entries(o).sort()));

var p = Object.assign({}, o, {b: 99, d: 4});
console.log("merged:", JSON.stringify(p));

console.log("hasOwn a:", Object.prototype.hasOwnProperty.call(o, "a"));
console.log("hasOwn z:", Object.prototype.hasOwnProperty.call(o, "z"));

var frozen = Object.freeze({x: 1});
console.log("isFrozen:", Object.isFrozen(frozen));
frozen.x = 99;
console.log("after attempted mutation:", frozen.x);
