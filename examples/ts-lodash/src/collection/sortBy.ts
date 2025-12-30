/**
 * Creates an array of elements, sorted in ascending order by the results
 * of running each element through iteratee.
 * 
 * @example
 * sortBy([{name: 'banana'}, {name: 'apple'}], (o) => o.name)
 * // => [{name: 'apple'}, {name: 'banana'}]
 * 
 * sortBy([3, 1, 2], (n) => n) // => [1, 2, 3]
 */
export function sortBy<T>(array: T[], iteratee: (value: T) => number): T[] {
    // Create a copy to avoid mutating the original
    const result: T[] = [];
    let i = 0;
    const len = array.length;
    while (i < len) {
        result.push(array[i]);
        i = i + 1;
    }
    
    // Sort using a comparison function based on iteratee results
    result.sort((a: T, b: T) => {
        const aKey = iteratee(a);
        const bKey = iteratee(b);
        if (aKey < bKey) return -1;
        if (aKey > bKey) return 1;
        return 0;
    });
    
    return result;
}

/**
 * Creates an array of elements, sorted in descending order by the results
 * of running each element through iteratee.
 * 
 * @example
 * sortByDesc([1, 2, 3], (n) => n) // => [3, 2, 1]
 */
export function sortByDesc<T>(array: T[], iteratee: (value: T) => number): T[] {
    // Create a copy to avoid mutating the original
    const result: T[] = [];
    let i = 0;
    const len = array.length;
    while (i < len) {
        result.push(array[i]);
        i = i + 1;
    }
    
    // Sort in descending order
    result.sort((a: T, b: T) => {
        const aKey = iteratee(a);
        const bKey = iteratee(b);
        if (aKey > bKey) return -1;
        if (aKey < bKey) return 1;
        return 0;
    });
    
    return result;
}
