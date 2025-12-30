/**
 * Reduces collection to a value which is the accumulated result of running
 * each element through iteratee.
 * 
 * @example
 * reduce([1, 2, 3], (sum, n) => sum + n, 0) // => 6
 */
export function reduce<T, U>(array: T[], iteratee: (accumulator: U, value: T) => U, initialValue: U): U {
    let accumulator = initialValue;
    let i = 0;
    const len = array.length;
    
    while (i < len) {
        accumulator = iteratee(accumulator, array[i]);
        i = i + 1;
    }
    
    return accumulator;
}

/**
 * Like reduce except that it iterates from right to left.
 * 
 * @example
 * reduceRight([[0, 1], [2, 3], [4, 5]], (flattened, other) => { ... }, []) 
 * // => [4, 5, 2, 3, 0, 1]
 */
export function reduceRight<T, U>(array: T[], iteratee: (accumulator: U, value: T) => U, initialValue: U): U {
    let accumulator = initialValue;
    let i = array.length - 1;
    
    while (i >= 0) {
        accumulator = iteratee(accumulator, array[i]);
        i = i - 1;
    }
    
    return accumulator;
}
