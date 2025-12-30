// Test chunk function which uses nested loops
function chunk<T>(array: T[], size: number): T[][] {
    if (size <= 0) {
        return [];
    }
    
    const result: T[][] = [];
    let i = 0;
    const len = array.length;
    
    while (i < len) {
        const chunk: T[] = [];
        let j = 0;
        while (j < size && i + j < len) {
            chunk.push(array[i + j]);
            j = j + 1;
        }
        result.push(chunk);
        i = i + size;
    }
    
    return result;
}

const arr = [1, 2, 3, 4, 5];
const chunks = chunk(arr, 2);
console.log(chunks.length);  // Should be 3: [[1,2], [3,4], [5]]
