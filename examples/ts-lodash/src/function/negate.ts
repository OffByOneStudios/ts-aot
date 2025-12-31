/**
 * Creates a function that negates the result of the predicate `fn`.
 * 
 * @example
 * const isOdd = negate((n: number) => n % 2 === 0);
 * isOdd(1) // => true
 * isOdd(2) // => false
 */
export function negate<T>(fn: (x: T) => boolean): (x: T) => boolean {
    return (x: T): boolean => !fn(x);
}
