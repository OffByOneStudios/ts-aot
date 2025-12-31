/**
 * Creates a function that memoizes the result of `fn`.
 * Uses a Map to cache results based on the first argument.
 * 
 * @example
 * const expensive = memoize((n: number) => {
 *   console.log("computing...");
 *   return n * 2;
 * });
 * expensive(5); // logs "computing...", returns 10
 * expensive(5); // returns 10 (cached, no log)
 */
export function memoize<K, V>(fn: (key: K) => V): (key: K) => V {
    const cache = new Map<K, V>();
    return (key: K): V => {
        if (cache.has(key)) {
            return cache.get(key) as V;
        }
        const result = fn(key);
        cache.set(key, result);
        return result;
    };
}
