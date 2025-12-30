/**
 * Creates a duplicate-free version of an array.
 * 
 * @example
 * uniq([2, 1, 2]) // => [2, 1]
 * uniq([1, 1, 2, 2, 3]) // => [1, 2, 3]
 */
export function uniq<T>(array: T[]): T[] {
    const result: T[] = [];
    const seen = new Set<T>();
    
    let i = 0;
    const len = array.length;
    while (i < len) {
        const value = array[i];
        if (!seen.has(value)) {
            seen.add(value);
            result.push(value);
        }
        i = i + 1;
    }
    
    return result;
}

/**
 * Creates a duplicate-free version of an array using a function to compute uniqueness.
 * 
 * @example
 * uniqBy([2.1, 1.2, 2.3], Math.floor) // => [2.1, 1.2]
 * uniqBy([{ x: 1 }, { x: 2 }, { x: 1 }], o => o.x) // => [{ x: 1 }, { x: 2 }]
 */
export function uniqBy<T, K>(array: T[], iteratee: (value: T) => K): T[] {
    const result: T[] = [];
    const seen = new Set<K>();
    
    let i = 0;
    const len = array.length;
    while (i < len) {
        const value = array[i];
        const computed = iteratee(value);
        if (!seen.has(computed)) {
            seen.add(computed);
            result.push(value);
        }
        i = i + 1;
    }
    
    return result;
}
