// Isolate the slice assignment bug
const source = [1, 2, 3, 4];
console.log('source:', JSON.stringify(source));

// Get a slice
const slice = source.slice(0, 2);
console.log('slice from source.slice(0,2):', JSON.stringify(slice));
console.log('typeof slice:', typeof slice);
console.log('slice.length:', slice.length);
console.log('Array.isArray(slice):', Array.isArray(slice));

// Now try to assign to array
const result: number[][] = [];
console.log('\nBefore push, result:', JSON.stringify(result));

result.push(slice);
console.log('After push, result:', JSON.stringify(result));

// Try direct index assignment
const result2: number[][] = new Array(2);
console.log('\nresult2 initial:', JSON.stringify(result2));

// Assign the slice variable
result2[0] = slice;
console.log('After result2[0] = slice:', JSON.stringify(result2));

// Assign a fresh slice
result2[1] = source.slice(2, 4);
console.log('After result2[1] = source.slice(2,4):', JSON.stringify(result2));
