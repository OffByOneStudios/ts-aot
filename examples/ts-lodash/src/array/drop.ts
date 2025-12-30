/**
 * Creates a slice of array with n elements dropped from the beginning.
 * 
 * @example
 * drop([1, 2, 3], 2) // => [3]
 * drop([1, 2, 3], 5) // => []
 * drop([1, 2, 3], 0) // => [1, 2, 3]
 */
export function drop<T>(array: T[], n: number): T[] {
    if (n <= 0) {
        // Return a copy
        const result: T[] = [];
        let i = 0;
        while (i < array.length) {
            result.push(array[i]);
            i = i + 1;
        }
        return result;
    }
    
    const result: T[] = [];
    let i = n;
    const len = array.length;
    
    while (i < len) {
        result.push(array[i]);
        i = i + 1;
    }
    
    return result;
}

/**
 * Creates a slice of array with n elements dropped from the end.
 * 
 * @example
 * dropRight([1, 2, 3], 2) // => [1]
 * dropRight([1, 2, 3], 5) // => []
 * dropRight([1, 2, 3], 0) // => [1, 2, 3]
 */
export function dropRight<T>(array: T[], n: number): T[] {
    const len = array.length;
    const end = n >= len ? 0 : len - n;
    const result: T[] = [];
    let i = 0;
    
    while (i < end) {
        result.push(array[i]);
        i = i + 1;
    }
    
    return result;
}
