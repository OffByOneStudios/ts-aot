
function testWhileAlias(arr: number[]) {
    const len = arr.length;
    let i = 0;
    let sum = 0;
    while (i < len) {
        sum += arr[i];
        i++;
    }
    return sum;
}

const a = [1, 2, 3, 4, 5];
console.log(testWhileAlias(a));
