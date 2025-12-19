// Test String parity
const s = "hello world";
console.log(s.replace("world", "copilot"));
console.log(s.replaceAll("l", "L"));
console.log("abc".repeat(3));
console.log("5".padStart(3, "0"));
console.log("5".padEnd(3, "!"));

// Test Array parity
const arr = [1, 2, 3, 4, 5];
arr.forEach((v, i) => {
    console.log("val: " + v + " at " + i);
});

const doubled = arr.map(v => v * 2);
console.log("doubled: " + doubled.join(","));

const even = arr.filter(v => v % 2 === 0);
console.log("even: " + even.join(","));

const sum = arr.reduce((acc, v) => acc + v, 0);
console.log("sum: " + sum);

console.log("some even: " + arr.some(v => v % 2 === 0));
console.log("every even: " + arr.every(v => v % 2 === 0));

const found = arr.find(v => v > 3);
console.log("found > 3: " + found);

const foundIdx = arr.findIndex(v => v > 3);
console.log("found index > 3: " + foundIdx);

// Test Map parity
const map = new Map<string, number>();
map.set("a", 1);
map.set("b", 2);
map.set("c", 3);

console.log("map has a: " + map.has("a"));
map.delete("a");
console.log("map has a after delete: " + map.has("a"));

map.forEach((v, k) => {
    console.log("key: " + k + " val: " + v);
});

map.clear();
console.log("map size after clear: " + map.size);
