const _ = require('./lodash_full.js');

console.log('Lodash loaded, typeof _:', typeof _);
console.log('typeof _.chunk:', typeof _.chunk);

// Test _.chunk
const arr = [1, 2, 3, 4, 5, 6, 7];
const chunked = _.chunk(arr, 3);
console.log('_.chunk([1,2,3,4,5,6,7], 3):', JSON.stringify(chunked));

// Test _.map
const mapped = _.map([1, 2, 3], (n: number) => n * 2);
console.log('_.map([1,2,3], n => n*2):', JSON.stringify(mapped));

// Test _.filter
const filtered = _.filter([1, 2, 3, 4], (n: number) => n % 2 === 0);
console.log('_.filter([1,2,3,4], n => n%2===0):', JSON.stringify(filtered));

console.log('✅ Full lodash test complete!');
