/**
 * Creates a function that returns `value`.
 * 
 * @example
 * const always42 = constant(42);
 * always42() // => 42
 */
export function constant<T>(value: T): () => T {
    return () => value;
}
