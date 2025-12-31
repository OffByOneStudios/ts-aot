// Simple sort test - just the copying part

function simpleCopy<T>(array: T[], iteratee: (value: T) => number): T[] {
    const result: T[] = [];
    let i = 0;
    const len = array.length;
    while (i < len) {
        result.push(array[i]);
        i = i + 1;
    }
    return result;
}

console.log("Testing simple copy:");
const nums = [3, 1, 4, 1, 5];
const copied = simpleCopy(nums, (n: number) => n);
console.log("Length:");
console.log(copied.length);
console.log("First:");
console.log(copied[0]);

console.log("Done!");
