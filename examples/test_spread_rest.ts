// Test spread into functions with rest parameters

// Function with only rest parameter
function sumAll(...values: number[]): number {
    let total = 0;
    for (let i = 0; i < values.length; i++) {
        total = total + values[i];
    }
    return total;
}

// Test 1: Direct call with rest params
const r1 = sumAll(1, 2, 3, 4);
console.log("sumAll(1,2,3,4) =", r1);

// Test 2: Spread into rest params - should pass array directly
const nums = [10, 20, 30, 40];
const r2 = sumAll(...nums);
console.log("sumAll(...[10,20,30,40]) =", r2);

// Verify
if (r1 === 10 && r2 === 100) {
    console.log("PASS: All tests passed");
} else {
    console.log("FAIL: r1 =", r1, ", r2 =", r2);
}
