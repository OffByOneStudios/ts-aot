// Simulate what lodash's chunk function does
// Using JavaScript-style dynamic access

const array = [1, 2, 3, 4, 5, 6, 7];
const size = 3;

// Simulate baseSlice
function baseSlice(arr: any, start: number, end: number): any {
    let index = -1;
    let length = end - start;
    if (length < 0) length = 0;
    const result = new Array(length);
    
    while (++index < length) {
        result[index] = arr[index + start];
    }
    return result;
}

// Simulate chunk
function chunk(arr: any, size: number): any {
    if (size < 1) return [];
    
    const length = arr.length;
    let index = 0;
    let resIndex = 0;
    const result = new Array(Math.ceil(length / size));
    
    console.log('chunk: length=', length, 'size=', size, 'result array size=', result.length);
    
    while (index < length) {
        console.log('chunk loop: index=', index, 'resIndex=', resIndex);
        const sliced = baseSlice(arr, index, index + size);
        console.log('  sliced=', JSON.stringify(sliced));
        result[resIndex++] = sliced;
        console.log('  after assignment, result=', JSON.stringify(result));
        index += size;
    }
    
    return result;
}

const result = chunk(array, size);
console.log('Final result:', JSON.stringify(result));
