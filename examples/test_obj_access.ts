// Simple test: object property access

const obj = {
    a: 1,
    b: 2,
    c: 3
};

console.log("Direct access:");
console.log("obj.a = " + obj.a);
console.log("obj.b = " + obj.b);

console.log("Computed access:");
const key = "a";
console.log("obj[key] = " + obj[key]);

console.log("For-in access:");
for (const k in obj) {
    console.log("k=" + k + " obj[k]=" + obj[k]);
}

console.log("Done");
