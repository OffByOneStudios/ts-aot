var obj = {name: "alice", age: 30, tags: ["a", "b", "c"], nested: {v: 1}};
var s = JSON.stringify(obj);
console.log("stringified:", s);

var parsed = JSON.parse(s);
console.log("parsed.name:", parsed.name);
console.log("parsed.age:", parsed.age);
console.log("parsed.tags:", parsed.tags.join(","));
console.log("parsed.nested.v:", parsed.nested.v);

console.log("stringify [1,2,3]:", JSON.stringify([1, 2, 3]));
console.log("stringify null:", JSON.stringify(null));
console.log("stringify undef field:", JSON.stringify({x: undefined, y: 1}));
console.log("indent 2:", JSON.stringify({a: 1, b: 2}, null, 2));
