// Test spread in function calls

function sum(a: number, b: number, c: number): number {
    return a + b + c;
}

const args = [1, 2, 3];
const result = sum(...args);
console.log("sum(...[1,2,3]) =", result);

// Verify expected output
if (result === 6) {
    console.log("PASS: sum(...[1,2,3]) = 6");
} else {
    console.log("FAIL: expected 6, got", result);
}
