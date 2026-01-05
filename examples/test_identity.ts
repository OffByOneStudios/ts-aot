// Test lodash identity to see return value handling
const _ = require('./lodash_full.js');

function callIdentity(v: any): any {
    return _.identity(v);
}

console.log('Test 1: _.identity(2) directly');
console.log('Result:', _.identity(2));

console.log('\nTest 2: callIdentity(2) through any');  
console.log('Result:', callIdentity(2));

console.log('\nTest 3: Passing a variable');
const x = 2;
console.log('Result:', _.identity(x));

console.log('\nTest 4: callIdentity with variable');
const y: any = 2;
console.log('Result:', callIdentity(y));
