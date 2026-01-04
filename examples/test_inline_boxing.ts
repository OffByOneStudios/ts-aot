// Test case for inline boxing - focused on property access performance
// This should work correctly after the inline boxing implementation

const obj: {[key: string]: number} = {
    a: 1,
    b: 2,
    c: 3,
    d: 4,
    e: 5
};

// Test 1: Simple for-in with computed access
console.log("Test 1: For-in with computed access");
let sum = 0;
for (const key in obj) {
    const val = obj[key];
    sum = sum + val;
    console.log("  " + key + " = " + val);
}
console.log("Sum: " + sum);

// Test 2: Multiple iterations (to measure performance)
console.log("\nTest 2: Multiple iterations");
let total = 0;
for (let i = 0; i < 1000; i++) {
    for (const key in obj) {
        total = total + obj[key];
    }
}
console.log("Total after 1000 iterations: " + total);

// Test 3: Store key, then access
console.log("\nTest 3: Key stored before access");
const keys: string[] = [];
for (const k in obj) {
    keys.push(k);
}
for (let i = 0; i < keys.length; i++) {
    const k = keys[i];
    console.log("  " + k + " = " + obj[k]);
}

console.log("\nDone");
