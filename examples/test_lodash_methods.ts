const _ = require('./lodash_full.js');

console.log('=== Testing direct lodash chunk ===');

// First test if _ is correct
console.log('typeof _:', typeof _);
console.log('_ is function:', typeof _ === 'function');

// Check chunk function
console.log('typeof _.chunk:', typeof _.chunk);

// Try calling chunk on a simple array
const simpleArr = [1, 2, 3, 4];
console.log('\nCalling _.chunk([1,2,3,4], 2)...');

// Add some diagnostics
console.log('simpleArr type:', typeof simpleArr);
console.log('simpleArr Array.isArray:', Array.isArray(simpleArr));
console.log('simpleArr.length:', simpleArr.length);

// Call chunk
const chunkResult = _.chunk(simpleArr, 2);

console.log('\nResult:');
console.log('typeof chunkResult:', typeof chunkResult);
console.log('Array.isArray(chunkResult):', Array.isArray(chunkResult));
console.log('chunkResult.length:', chunkResult.length);
console.log('JSON.stringify(chunkResult):', JSON.stringify(chunkResult));

// Also try _.map which is simpler
console.log('\n=== Testing _.map ===');
const doubled = _.map([1, 2, 3], (x: number) => x * 2);
console.log('_.map([1,2,3], x => x*2):', JSON.stringify(doubled));

// And _.filter
console.log('\n=== Testing _.filter ===');
const evens = _.filter([1, 2, 3, 4], (x: number) => x % 2 === 0);
console.log('_.filter([1,2,3,4], x => x%2===0):', JSON.stringify(evens));
