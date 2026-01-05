// Test typeof with any parameter
const _ = require('./lodash_full.js');

function testTypeof(value: any): void {
    console.log('value:', value);
    console.log('typeof value:', typeof value);
    console.log("typeof value == 'number':", typeof value == 'number');
}

console.log('=== Test 1: Passing literal 2 ===');
testTypeof(2);

console.log('\n=== Test 2: Passing variable num ===');
const num = 2;
testTypeof(num);

console.log('\n=== Test 3: Calling lodash toNumber with any ===');
function callToNumber(v: any): any {
    return _.toNumber(v);
}
console.log('callToNumber(2):', callToNumber(2));
