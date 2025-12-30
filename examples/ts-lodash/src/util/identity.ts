/**
 * Returns the first argument it receives.
 * 
 * @example
 * identity(42) // => 42
 * identity("hello") // => "hello"
 */
export function identity<T>(value: T): T {
    return value;
}
