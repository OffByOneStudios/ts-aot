// Debug: Check if ts_map_get returns anything

const obj = {
    a: 1,
    b: 2
};

console.log("Testing obj[a]:");
const key = "a";
const val = obj[key];
console.log("val type: " + typeof val);
console.log("val: " + val);

console.log("Done");
