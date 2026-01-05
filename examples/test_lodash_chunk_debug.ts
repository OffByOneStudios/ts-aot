const _ = require('./lodash_full.js');

console.log('Testing _.chunk step by step...');
console.log('typeof _.chunk:', typeof _.chunk);

const testArray = [1, 2, 3, 4];
console.log('Input array:', JSON.stringify(testArray));
console.log('Array length:', testArray.length);

console.log('\nCalling _.chunk(testArray, 2)...');
const result = _.chunk(testArray, 2);

console.log('Result:', JSON.stringify(result));
console.log('Result type:', typeof result);
console.log('Result length:', result ? result.length : 'N/A');

if (Array.isArray(result)) {
    console.log('Result is an array');
    for (let i = 0; i < result.length; i++) {
        console.log(`  result[${i}]:`, JSON.stringify(result[i]));
    }
} else {
    console.log('Result is NOT an array!');
}
