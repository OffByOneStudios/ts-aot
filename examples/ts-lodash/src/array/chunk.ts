/**
 * Creates an array of elements split into groups of `size`.
 * If array can't be split evenly, the final chunk will contain remaining elements.
 * 
 * @example
 * chunk(['a', 'b', 'c', 'd'], 2) // => [['a', 'b'], ['c', 'd']]
 * chunk(['a', 'b', 'c', 'd'], 3) // => [['a', 'b', 'c'], ['d']]
 */
export function chunk<T>(array: T[], size: number): T[][] {
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
