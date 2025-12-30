/**
 * Iterates over elements of collection, invoking iteratee for each element.
 * 
 * @example
 * forEach([1, 2, 3], (n) => console.log(n))
 */
export function forEach<T>(array: T[], iteratee: (value: T) => void): void {
    let i = 0;
    const len = array.length;
    
    while (i < len) {
        iteratee(array[i]);
        i = i + 1;
    }
}

/**
 * Like forEach but iterates from right to left.
 * 
 * @example
 * forEachRight([1, 2, 3], (n) => console.log(n)) // logs 3, 2, 1
 */
export function forEachRight<T>(array: T[], iteratee: (value: T) => void): void {
    let i = array.length - 1;
    
    while (i >= 0) {
        iteratee(array[i]);
        i = i - 1;
    }
}
