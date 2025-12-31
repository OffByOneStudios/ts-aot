// Simple callback test

function applyCallback<T>(value: T, fn: (v: T) => T): T {
    return fn(value);
}

console.log("Testing simple callback:");
const result = applyCallback(5, (n: number) => n * 2);
console.log("5 * 2 =");
console.log(result);

console.log("Done!");
