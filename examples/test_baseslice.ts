// Test lodash's baseSlice logic
function baseSlice(array: number[], start: number, end: number): number[] {
    console.log('baseSlice called with array:', JSON.stringify(array), 'start:', start, 'end:', end);
    var index = -1;
    var length = array.length;
    console.log('  initial length:', length);

    if (start < 0) {
        start = -start > length ? 0 : (length + start);
    }
    console.log('  adjusted start:', start);
    
    end = end > length ? length : end;
    console.log('  adjusted end:', end);
    
    if (end < 0) {
        end += length;
    }
    
    console.log('  before >>> : end - start =', end - start);
    length = start > end ? 0 : ((end - start) >>> 0);
    console.log('  after >>> length:', length);
    
    start = start >>> 0;
    console.log('  start >>> 0:', start);

    var result = new Array(length);
    console.log('  created result array of length:', result.length);
    
    while (++index < length) {
        result[index] = array[index + start];
    }
    console.log('  result:', JSON.stringify(result));
    return result;
}

// Test it
const arr = [1, 2, 3, 4, 5, 6, 7];
console.log('Input:', JSON.stringify(arr));

const slice1 = baseSlice(arr, 0, 3);
console.log('baseSlice(arr, 0, 3):', JSON.stringify(slice1));

const slice2 = baseSlice(arr, 3, 6);
console.log('baseSlice(arr, 3, 6):', JSON.stringify(slice2));

const slice3 = baseSlice(arr, 6, 7);
console.log('baseSlice(arr, 6, 7):', JSON.stringify(slice3));
