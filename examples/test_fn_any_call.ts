// Test: function assigned to any can be called

function add(a: number, b: number): number {
    return a + b;
}

// Assign function to any variable
let fn: any = add;

console.log("typeof fn:", typeof fn);

// Call it
let result = fn(3, 5);
console.log("result:", result);

console.log("Done");
