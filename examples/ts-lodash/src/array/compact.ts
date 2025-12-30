/**
 * Creates an array with all falsy values removed.
 * The values false, null, 0, "", undefined, and NaN are falsy.
 * 
 * Note: This implementation checks for 0 and empty string.
 * null, undefined, and NaN handling depends on runtime support.
 * 
 * @example
 * compact([0, 1, false, 2, '', 3]) // => [1, 2, 3]
 */
export function compact<T>(array: T[]): T[] {
    const result: T[] = [];
    let i = 0;
    const len = array.length;
    
    while (i < len) {
        const value = array[i];
        // Check for truthy values
        // In TypeScript, we can use type narrowing
        if (value) {
            result.push(value);
        }
        i = i + 1;
    }
    
    return result;
}
