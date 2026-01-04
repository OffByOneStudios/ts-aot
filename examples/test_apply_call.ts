// Test Function.prototype.apply pattern
console.log("Start");

function add(a: number, b: number): number {
    return a + b;
}

// Test .apply
const result1 = add.apply(null, [1, 2]);
console.log("apply result:", result1);

// Test .call
const result2 = add.call(null, 3, 4);
console.log("call result:", result2);

console.log("Done");
