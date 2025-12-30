/**
 * Iterates over elements of collection, returning an array of all elements predicate returns truthy for.
 * 
 * @example
 * filter([1, 2, 3, 4], (n) => n % 2 === 0) // => [2, 4]
 */
export function filter<T>(array: T[], predicate: (value: T) => boolean): T[] {
    const result: T[] = [];
    let i = 0;
    const len = array.length;
    
    while (i < len) {
        if (predicate(array[i])) {
            result.push(array[i]);
        }
        i = i + 1;
    }
    
    return result;
}

/**
 * The opposite of filter; returns elements predicate returns falsy for.
 * 
 * @example
 * reject([1, 2, 3, 4], (n) => n % 2 === 0) // => [1, 3]
 */
export function reject<T>(array: T[], predicate: (value: T) => boolean): T[] {
    const result: T[] = [];
    let i = 0;
    const len = array.length;
    
    while (i < len) {
        if (!predicate(array[i])) {
            result.push(array[i]);
        }
        i = i + 1;
    }
    
    return result;
}
