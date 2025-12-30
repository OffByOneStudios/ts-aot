/**
 * Creates an array of values by running each element through iteratee.
 * 
 * @example
 * map([1, 2, 3], (n) => n * 2) // => [2, 4, 6]
 */
export function map<T, U>(array: T[], iteratee: (value: T) => U): U[] {
    const result: U[] = [];
    let i = 0;
    const len = array.length;
    
    while (i < len) {
        result.push(iteratee(array[i]));
        i = i + 1;
    }
    
    return result;
}

/**
 * Creates a flattened array of values by running each element through iteratee
 * and flattening the mapped results.
 * 
 * @example
 * flatMap([1, 2], (n) => [n, n]) // => [1, 1, 2, 2]
 */
export function flatMap<T, U>(array: T[], iteratee: (value: T) => U[]): U[] {
    const result: U[] = [];
    let i = 0;
    const len = array.length;
    
    while (i < len) {
        const mapped = iteratee(array[i]);
        let j = 0;
        while (j < mapped.length) {
            result.push(mapped[j]);
            j = j + 1;
        }
        i = i + 1;
    }
    
    return result;
}
