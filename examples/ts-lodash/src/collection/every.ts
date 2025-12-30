/**
 * Checks if predicate returns truthy for all elements of collection.
 * 
 * @example
 * every([1, 2, 3], (n) => n > 0) // => true
 * every([1, 2, 3], (n) => n > 2) // => false
 */
export function every<T>(array: T[], predicate: (value: T) => boolean): boolean {
    let i = 0;
    const len = array.length;
    
    while (i < len) {
        if (!predicate(array[i])) {
            return false;
        }
        i = i + 1;
    }
    
    return true;
}

/**
 * Checks if predicate returns truthy for any element of collection.
 * 
 * @example
 * some([1, 2, 3], (n) => n > 2) // => true
 * some([1, 2, 3], (n) => n > 5) // => false
 */
export function some<T>(array: T[], predicate: (value: T) => boolean): boolean {
    let i = 0;
    const len = array.length;
    
    while (i < len) {
        if (predicate(array[i])) {
            return true;
        }
        i = i + 1;
    }
    
    return false;
}
