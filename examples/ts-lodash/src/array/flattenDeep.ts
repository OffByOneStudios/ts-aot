/**
 * Recursively flattens array to a specified depth.
 * 
 * @example
 * flattenDepth([[1, [2]], [3, [4]]], 1) // => [1, [2], 3, [4]]
 * flattenDepth([[1, [2]], [3, [4]]], 2) // => [1, 2, 3, 4]
 */
export function flattenDepth<T>(array: T[][], depth: number): T[] {
    if (depth <= 0) {
        // At depth 0, just return a copy of the outer array as T[]
        const result: T[] = [];
        let i = 0;
        while (i < array.length) {
            result.push(array[i] as unknown as T);
            i = i + 1;
        }
        return result;
    }
    
    // Flatten one level
    const result: T[] = [];
    let i = 0;
    while (i < array.length) {
        const inner = array[i];
        let j = 0;
        while (j < inner.length) {
            result.push(inner[j]);
            j = j + 1;
        }
        i = i + 1;
    }
    
    return result;
}

/**
 * Flattens a 2D array (array of arrays) to a 1D array.
 * This is the same as flatten() but provided as flattenDeep for simple cases.
 * 
 * Note: True recursive flattenDeep with mixed types requires Array.isArray()
 * which is not yet implemented in ts-aot.
 * 
 * @example
 * flattenDeep([[1, 2], [3, 4]]) // => [1, 2, 3, 4]
 */
export function flattenDeep<T>(array: T[][]): T[] {
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
