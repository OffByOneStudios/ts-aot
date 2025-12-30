/**
 * Invokes the iteratee `n` times, returning an array of the results.
 * 
 * @example
 * times(3, (i) => i * 2) // => [0, 2, 4]
 * times(4, () => 'x') // => ['x', 'x', 'x', 'x']
 */
export function times<T>(n: number, iteratee: (index: number) => T): T[] {
    const result: T[] = [];
    let i = 0;
    while (i < n) {
        result.push(iteratee(i));
        i = i + 1;
    }
    return result;
}
