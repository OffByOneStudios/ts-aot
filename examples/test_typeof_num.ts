// Minimal test for the typeof issue
const _ = require('./lodash_full.js');

const num = 2;
console.log('num:', num);
console.log('typeof num:', typeof num);

// Test string comparison
const typeStr = typeof num;
console.log('typeStr:', typeStr);
console.log("typeStr == 'number':", typeStr == 'number');

// Now test what lodash sees
console.log('\n=== Testing lodash toNumber ===');

// toNumber source:
// function toNumber(value) {
//   if (typeof value == 'number') {
//     return value;
//   }
//   ...
// }

// So if typeof 2 returns 'number' as a string, it should work
const result = _.toNumber(num);
console.log('_.toNumber(num):', result);
console.log('typeof result:', typeof result);
