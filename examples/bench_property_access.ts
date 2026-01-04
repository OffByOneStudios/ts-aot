// Benchmark for property access performance
// Measures the time to perform many computed property accesses

const obj: {[key: string]: number} = {
    a: 1, b: 2, c: 3, d: 4, e: 5,
    f: 6, g: 7, h: 8, i: 9, j: 10
};

const iterations = 100000;

console.log("Property Access Benchmark");
console.log("=========================");
console.log("Iterations: " + iterations);

const start = Date.now();

let total = 0;
for (let i = 0; i < iterations; i++) {
    for (const key in obj) {
        total = total + obj[key];
    }
}

const end = Date.now();
const elapsed = end - start;

console.log("Total sum: " + total);
console.log("Time: " + elapsed + "ms");
console.log("Ops/sec: " + Math.floor((iterations * 10) / (elapsed / 1000)));

console.log("\nDone");
