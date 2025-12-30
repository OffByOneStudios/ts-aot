// Test simple array copy in generic function
function copyArray<T>(arr: T[]): T[] {
    const result: T[] = [];
    let i = 0;
    while (i < arr.length) {
        result.push(arr[i]);
        i = i + 1;
    }
    return result;
}

const nums = [3, 1, 4, 1, 5];
const copied = copyArray(nums);
console.log("copied length:", copied.length);

let i = 0;
while (i < copied.length) {
    console.log(copied[i]);
    i = i + 1;
}
