/**
 * Flattens array a single level deep.
 * 
 * @example
 * flatten([[1, 2], [3, 4]]) // => [1, 2, 3, 4]
 * flatten([[1], [[2]], [3]]) // => [1, [2], 3]
 */
export function flatten<T>(array: T[][]): T[] {
    const result: T[] = [];
    let i = 0;
    const len = array.length;
    
    while (i < len) {
        const inner = array[i];
        let j = 0;
        const innerLen = inner.length;
        while (j < innerLen) {
            result.push(inner[j]);
            j = j + 1;
        }
        i = i + 1;
    }
    
    return result;
}
