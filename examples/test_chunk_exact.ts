// Exact simulation of lodash's chunk internal structure
// Using var declarations like lodash does

function baseSlice(array: any, start: any, end: any): any {
    var index = -1;
    var length = end - start;
    var result = new Array(length);
    
    while (++index < length) {
        result[index] = array[index + start];
    }
    return result;
}

function chunk(array: any, size: any): any {
    // Mimic lodash's exact variable pattern
    var length = array == null ? 0 : array.length;
    console.log('chunk: length =', length);
    
    if (!length || size < 1) {
        console.log('chunk: returning early due to !length or size < 1');
        return [];
    }
    
    var index = 0;
    var resIndex = 0;
    var result = new Array(Math.ceil(length / size));
    console.log('chunk: result array created with size', result.length);
    
    while (index < length) {
        console.log('chunk while: index =', index, 'resIndex =', resIndex);
        var sliced = baseSlice(array, index, index + size);
        console.log('chunk while: sliced =', JSON.stringify(sliced));
        
        // This is the critical assignment
        result[resIndex++] = sliced;
        console.log('chunk while: after assignment, result[' + (resIndex-1) + '] =', JSON.stringify(result[resIndex-1]));
        console.log('chunk while: full result =', JSON.stringify(result));
        
        index = index + size;
    }
    
    return result;
}

const arr = [1, 2, 3, 4];
console.log('Input:', JSON.stringify(arr));
const r = chunk(arr, 2);
console.log('Final result:', JSON.stringify(r));
