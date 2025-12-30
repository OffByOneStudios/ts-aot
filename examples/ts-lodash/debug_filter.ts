/**
 * Debug test for filter
 */

function isTwo(n: number): boolean {
    return n === 2;
}

function filterTwos(arr: number[]): number[] {
    const result: number[] = [];
    let i = 0;
    while (i < arr.length) {
        const val = arr[i];
        if (isTwo(val)) {
            result.push(val);
        }
        i = i + 1;
    }
    return result;
}

const nums = [1, 2, 3, 2, 5];

console.log("=== Testing isTwo ===");
console.log("isTwo(2):");
console.log(isTwo(2));
console.log("isTwo(3):");
console.log(isTwo(3));

console.log("=== Testing filterTwos ===");
const twos = filterTwos(nums);
console.log("Result count:");
console.log(twos.length);
console.log("Values:");
let j = 0;
while (j < twos.length) {
    console.log(twos[j]);
    j = j + 1;
}
console.log("Done");
