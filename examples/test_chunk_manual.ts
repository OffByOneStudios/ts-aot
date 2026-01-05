// Debug chunk behavior step by step
const arr = [1, 2, 3, 4];
console.log('arr:', JSON.stringify(arr));
console.log('arr.length:', arr.length);

// Test array == null
console.log('arr == null:', arr == null);

// Test the length logic lodash uses
const length = arr == null ? 0 : arr.length;
console.log('computed length:', length);

// Test nativeCeil and size
const size = 2;
console.log('size:', size);

// Create result array
const resultLen = Math.ceil(length / size);
console.log('resultLen:', resultLen);

const result: number[][] = [];
console.log('result before loop:', JSON.stringify(result));

// Manual chunk logic
let index = 0;
let resIndex = 0;
while (index < length) {
    const end = Math.min(index + size, length);
    const slice = arr.slice(index, end);
    console.log(`  Slice [${index}:${end}]:`, JSON.stringify(slice));
    result.push(slice);
    index += size;
}

console.log('result after loop:', JSON.stringify(result));
console.log('result.length:', result.length);
