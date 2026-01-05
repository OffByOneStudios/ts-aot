// Simulate lodash chunk to debug
const _ = require('./lodash_full.js');

// Create an array
const myArr = [1, 2, 3, 4];
console.log('myArr:', JSON.stringify(myArr));
console.log('typeof myArr:', typeof myArr);
console.log('myArr.length:', myArr.length);

// Check if lodash sees the array correctly
console.log('\n=== Calling internal lodash functions ===');

// Try baseSlice directly if accessible
const size = 2;

// Check nativeMax and toInteger through lodash
console.log('\n=== Testing Math functions ===');
console.log('Math.max(2, 0):', Math.max(2, 0));
console.log('Math.ceil(4/2):', Math.ceil(4 / 2));

// Create array like lodash does
const resultLen = Math.ceil(4 / 2);
console.log('Array(' + resultLen + '):', JSON.stringify(new Array(resultLen)));

// Test the while loop
console.log('\n=== Testing while loop ===');
const result: number[][] = new Array(resultLen);
let index = 0;
let resIndex = 0;
while (index < 4) {
    const end = index + size;
    const slice = myArr.slice(index, end);
    console.log('slice [' + index + ':' + end + ']:', JSON.stringify(slice));
    result[resIndex++] = slice;
    index = end;
}
console.log('result:', JSON.stringify(result));

// Now let's try passing the array to a function
console.log('\n=== Testing function parameter ===');
function testFn(arr: any) {
    console.log('Inside testFn, arr:', JSON.stringify(arr));
    console.log('arr.length:', arr.length);
    console.log('arr[0]:', arr[0]);
}
testFn(myArr);

// What about when we pass to a JS function via require?
console.log('\n=== Testing lodash identity ===');
const identity = _.identity;
console.log('typeof identity:', typeof identity);
const identityResult = identity(myArr);
console.log('identity(myArr):', JSON.stringify(identityResult));
