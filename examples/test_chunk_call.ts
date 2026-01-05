// Debug lodash chunk specifically
const _ = require('./lodash_full.js');

console.log('typeof _:', typeof _);
console.log('typeof _.chunk:', typeof _.chunk);

// Try calling chunk with very simple args
const arr = [1, 2, 3, 4];
console.log('Input array:', JSON.stringify(arr));

// Call chunk
const result = _.chunk(arr, 2);
console.log('typeof result:', typeof result);
console.log('result:', JSON.stringify(result));

// Try accessing properties
if (result) {
    console.log('result.length:', result.length);
    if (result.length > 0) {
        console.log('result[0]:', JSON.stringify(result[0]));
    }
}
