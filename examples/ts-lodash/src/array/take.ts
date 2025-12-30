/**
 * Creates a slice of array with n elements taken from the beginning.
 * 
 * @example
 * take([1, 2, 3], 2) // => [1, 2]
 * take([1, 2, 3], 5) // => [1, 2, 3]
 * take([1, 2, 3], 0) // => []
 */
export function take<T>(array: T[], n: number): T[] {
    if (n <= 0) {
        return [];
    }
    
    const result: T[] = [];
    let i = 0;
    const len = array.length;
    const count = n < len ? n : len;
    
    while (i < count) {
        result.push(array[i]);
        i = i + 1;
    }
    
    return result;
}

/**
 * Creates a slice of array with n elements taken from the end.
 * 
 * @example
 * takeRight([1, 2, 3], 2) // => [2, 3]
 * takeRight([1, 2, 3], 5) // => [1, 2, 3]
 * takeRight([1, 2, 3], 0) // => []
 */
export function takeRight<T>(array: T[], n: number): T[] {
    if (n <= 0) {
        return [];
    }
    
    const len = array.length;
    const start = n >= len ? 0 : len - n;
    const result: T[] = [];
    let i = start;
    
    while (i < len) {
        result.push(array[i]);
        i = i + 1;
    }
    
    return result;
}
