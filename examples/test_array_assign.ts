// Test array assignment with post-increment
const arr: number[][] = new Array(3);
console.log('Initial arr:', JSON.stringify(arr));

let idx = 0;

// Use post-increment like lodash does
arr[idx++] = [1, 2];
console.log('After arr[idx++] = [1,2], arr:', JSON.stringify(arr), 'idx:', idx);

arr[idx++] = [3, 4];
console.log('After arr[idx++] = [3,4], arr:', JSON.stringify(arr), 'idx:', idx);

arr[idx++] = [5];
console.log('After arr[idx++] = [5], arr:', JSON.stringify(arr), 'idx:', idx);

console.log('\nFinal result:', JSON.stringify(arr));

// Also test with pre-created slices
console.log('\n=== Test with slice ===');
const source = [1, 2, 3, 4, 5, 6];
const result: number[][] = new Array(3);
let i = 0;
let res = 0;
while (i < 6) {
    const slice = source.slice(i, i + 2);
    console.log('Assigning slice', JSON.stringify(slice), 'to index', res);
    result[res] = slice;
    console.log('After assignment, result:', JSON.stringify(result));
    res++;
    i += 2;
}
console.log('Final:', JSON.stringify(result));
