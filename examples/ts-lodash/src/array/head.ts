/**
 * Gets the first element of array.
 * 
 * @example
 * head([1, 2, 3]) // => 1
 * head([]) // => undefined
 */
export function head<T>(array: T[]): T {
    return array[0];
}

/**
 * Alias for head.
 */
export function first<T>(array: T[]): T {
    return array[0];
}

/**
 * Gets the last element of array.
 * 
 * @example
 * last([1, 2, 3]) // => 3
 * last([]) // => undefined
 */
export function last<T>(array: T[]): T {
    const len = array.length;
    if (len === 0) {
        return array[0]; // undefined
    }
    return array[len - 1];
}

/**
 * Gets all but the first element of array.
 * 
 * @example
 * tail([1, 2, 3]) // => [2, 3]
 */
export function tail<T>(array: T[]): T[] {
    const result: T[] = [];
    let i = 1;
    const len = array.length;
    while (i < len) {
        result.push(array[i]);
        i = i + 1;
    }
    return result;
}

/**
 * Gets all but the last element of array.
 * 
 * @example
 * initial([1, 2, 3]) // => [1, 2]
 */
export function initial<T>(array: T[]): T[] {
    const result: T[] = [];
    let i = 0;
    const len = array.length - 1;
    while (i < len) {
        result.push(array[i]);
        i = i + 1;
    }
    return result;
}
