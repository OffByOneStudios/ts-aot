
function testLocalAlias(arr: number[]) {
    const len = arr.length;
    let sum = 0;
    for (let i = 0; i < len; i++) {
        sum += arr[i];
    }
    return sum;
}

const a = [1, 2, 3, 4, 5];
console.log(testLocalAlias(a));
