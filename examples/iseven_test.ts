// Test isEven directly

function isEven(n: number): boolean {
    console.log("isEven: n=");
    console.log(n);
    let remainder = n % 2;
    console.log("isEven: remainder=");
    console.log(remainder);
    let result = remainder === 0;
    console.log("isEven: result=");
    console.log(result);
    return result;
}

console.log("Direct test:");
console.log("isEven(1):");
console.log(isEven(1));
console.log("isEven(2):");
console.log(isEven(2));
console.log("isEven(3):");
console.log(isEven(3));
console.log("isEven(4):");
console.log(isEven(4));
