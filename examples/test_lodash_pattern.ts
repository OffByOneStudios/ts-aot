// Test that mimics lodash's actual structure more closely

const result = require('./lodash_sim.js');

console.log('typeof result:', typeof result);
console.log('typeof result._:', typeof result._);
console.log('result() call:', result());

if (typeof result._ === 'function') {
  console.log('SUCCESS: Module exports a function with ._ property');
} else {
  console.log('FAIL: Expected result._ to be a function');
}
