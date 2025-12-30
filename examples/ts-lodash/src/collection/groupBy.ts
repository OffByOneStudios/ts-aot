/**
 * Creates a Map composed of keys generated from the results of running
 * each element through iteratee. The values are arrays of elements that
 * produced the corresponding key.
 * 
 * Note: Returns Map<K, T[]> instead of Record<string, T[]> for better 
 * ts-aot compatibility with non-string keys.
 * 
 * @example
 * groupBy([6.1, 4.2, 6.3], Math.floor)
 * // => Map { 4 => [4.2], 6 => [6.1, 6.3] }
 * 
 * groupBy(['one', 'two', 'three'], (s) => s.length)
 * // => Map { 3 => ['one', 'two'], 5 => ['three'] }
 */
export function groupBy<T, K>(array: T[], iteratee: (value: T) => K): Map<K, T[]> {
    const result = new Map<K, T[]>();
    let i = 0;
    const len = array.length;
    
    while (i < len) {
        const value = array[i];
        const key = iteratee(value);
        
        if (result.has(key)) {
            // Get the existing group and add to it
            // Note: We know it exists because we checked has()
            const existingGroup: T[] = result.get(key) as T[];
            existingGroup.push(value);
        } else {
            const newGroup: T[] = [];
            newGroup.push(value);
            result.set(key, newGroup);
        }
        
        i = i + 1;
    }
    
    return result;
}

/**
 * Creates a Map composed of keys generated from the results of running
 * each element through iteratee. Each key maps to the last element that
 * produced that key.
 * 
 * Note: Returns Map<K, T> instead of Record<string, T> for better 
 * ts-aot compatibility with non-string keys.
 * 
 * @example
 * keyBy([{id: 'a', val: 1}, {id: 'b', val: 2}], (o) => o.id)
 * // => Map { 'a' => {id: 'a', val: 1}, 'b' => {id: 'b', val: 2} }
 */
export function keyBy<T, K>(array: T[], iteratee: (value: T) => K): Map<K, T> {
    const result = new Map<K, T>();
    let i = 0;
    const len = array.length;
    
    while (i < len) {
        const value = array[i];
        const key = iteratee(value);
        result.set(key, value);
        i = i + 1;
    }
    
    return result;
}
