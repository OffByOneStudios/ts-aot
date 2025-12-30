/**
 * Creates an array of values not included in the other given arrays.
 * 
 * @example
 * difference([2, 1], [2, 3]) // => [1]
 * difference([1, 2, 3], [2], [3]) // => [1]
 */
export function difference<T>(array: T[], ...values: T[][]): T[] {
    // Build a Set of all values to exclude
    const excludeSet = new Set<T>();
    
    let i = 0;
    while (i < values.length) {
        const excludeArray = values[i];
        let j = 0;
        while (j < excludeArray.length) {
            excludeSet.add(excludeArray[j]);
            j = j + 1;
        }
        i = i + 1;
    }
    
    // Filter array to only include values not in excludeSet
    const result: T[] = [];
    let k = 0;
    while (k < array.length) {
        const value = array[k];
        if (!excludeSet.has(value)) {
            result.push(value);
        }
        k = k + 1;
    }
    
    return result;
}
