// Test literal vs variable
const _ = require('./lodash_full.js');

console.log('=== Testing with variable ===');
const num = 2;
const result1 = _.toNumber(num);
console.log('_.toNumber(num):', result1);

console.log('\n=== Testing with literal ===');
const result2 = _.toNumber(2);
console.log('_.toNumber(2):', result2);
