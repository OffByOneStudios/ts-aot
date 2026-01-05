// Minimal test for any parameter return
const _ = require('./lodash_full.js');

function wrapper(v: any): any {
    console.log('wrapper called, v =', v);
    console.log('typeof v =', typeof v);
    const result = _.identity(v);
    console.log('_.identity(v) returned =', result);
    return result;
}

console.log('Test 1: Direct _.identity(2)');
const r1 = _.identity(2);
console.log('r1 =', r1);

console.log('\nTest 2: wrapper(2)');
const r2 = wrapper(2);
console.log('r2 =', r2);
