/**
 * Creates a Map composed of keys generated from the results of running
 * each element through iteratee. The value is the last element that
 * produced the corresponding key.
 * 
 * Note: Returns Map<K, T> instead of Record<string, T> for better 
 * ts-aot compatibility with non-string keys.
 * 
 * @example
 * keyBy([{id: 'a', name: 'Alice'}, {id: 'b', name: 'Bob'}], (x) => x.id)
 * // => Map { 'a' => {id: 'a', name: 'Alice'}, 'b' => {id: 'b', name: 'Bob'} }
 * 
 * keyBy([1.2, 2.3, 3.4], Math.floor)
 * // => Map { 1 => 1.2, 2 => 2.3, 3 => 3.4 }
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
