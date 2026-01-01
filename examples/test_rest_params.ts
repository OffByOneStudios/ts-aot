// Test rest parameters

function sum(...numbers: number[]): number {
    let total = 0;
    let i = 0;
    for (i = 0; i < numbers.length; i++) {
        total = total + numbers[i];
    }
    return total;
}

console.log("=== Rest Parameters Test ===");

console.log("sum(1, 2, 3) =");
console.log(sum(1, 2, 3));

console.log("sum(10, 20) =");
console.log(sum(10, 20));

console.log("sum(1, 2, 3, 4, 5) =");
console.log(sum(1, 2, 3, 4, 5));

console.log("=== Test Complete ===");
