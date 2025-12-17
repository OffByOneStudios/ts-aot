let m = new Map();
m.set("foo", 42);
m.set("bar", 100);

console.log(m.get("foo"));
console.log(m.get("bar"));
if (m.has("foo")) {
    console.log(1);
} else {
    console.log(0);
}
if (m.has("baz")) {
    console.log(1);
} else {
    console.log(0);
}

m.set("foo", 99);
console.log(m.get("foo"));
