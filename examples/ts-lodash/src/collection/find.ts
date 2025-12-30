/**
 * Iterates over elements of collection, returning the first element predicate returns truthy for.
 * 
 * @example
 * find([1, 2, 3, 4], (n) => n > 2) // => 3
 */
export function find<T>(array: T[], predicate: (value: T) => boolean): T {
    let i = 0;
    const len = array.length;
    
    while (i < len) {
        if (predicate(array[i])) {
            return array[i];
        }
        i = i + 1;
    }
    
    // Return undefined - using array access beyond bounds
    return array[len];
}

/**
 * Like find but iterates from right to left.
 * 
 * @example
 * findLast([1, 2, 3, 4], (n) => n > 2) // => 4
 */
export function findLast<T>(array: T[], predicate: (value: T) => boolean): T {
    let i = array.length - 1;
    
    while (i >= 0) {
        if (predicate(array[i])) {
            return array[i];
        }
        i = i - 1;
    }
    
    // Return undefined
    return array[array.length];
}

/**
 * Returns the index of the first element predicate returns truthy for.
 * 
 * @example
 * findIndex([1, 2, 3, 4], (n) => n > 2) // => 2
 */
export function findIndex<T>(array: T[], predicate: (value: T) => boolean): number {
    let i = 0;
    const len = array.length;
    
    while (i < len) {
        if (predicate(array[i])) {
            return i;
        }
        i = i + 1;
    }
    
    return -1;
}

/**
 * Like findIndex but iterates from right to left.
 * 
 * @example
 * findLastIndex([1, 2, 3, 4], (n) => n > 2) // => 3
 */
export function findLastIndex<T>(array: T[], predicate: (value: T) => boolean): number {
    let i = array.length - 1;
    
    while (i >= 0) {
        if (predicate(array[i])) {
            return i;
        }
        i = i - 1;
    }
    
    return -1;
}
