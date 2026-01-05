const _ = require('./lodash_full.js');

console.log('Test 1: Array basics');
const arr = [1, 2, 3, 4, 5, 6];
console.log('arr:', JSON.stringify(arr));
console.log('arr.length:', arr.length);

console.log('\nTest 2: _.isArray');
console.log('_.isArray(arr):', _.isArray(arr));

console.log('\nTest 3: _.chunk');
const chunked = _.chunk(arr, 2);
console.log('_.chunk(arr, 2):', JSON.stringify(chunked));

console.log('\nTest 4: Manual chunk via baseSlice equivalent');
// This is what lodash chunk does internally
function myChunk(array: number[], size: number): number[][] {
    const length = array.length;
    if (!length || size < 1) {
        return [];
    }
    let index = 0;
    let resIndex = 0;
    const result = new Array(Math.ceil(length / size));
    
    console.log('  myChunk: length =', length, 'size =', size);
    console.log('  myChunk: result.length =', result.length);
    
    while (index < length) {
        const sliced = array.slice(index, index + size);
        console.log('  myChunk: sliced =', JSON.stringify(sliced));
        result[resIndex++] = sliced;
        index += size;
    }
    
    console.log('  myChunk: final result =', JSON.stringify(result));
    return result;
}

const myChunked = myChunk(arr, 2);
console.log('myChunk(arr, 2):', JSON.stringify(myChunked));
