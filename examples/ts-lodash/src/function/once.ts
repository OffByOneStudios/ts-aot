/**
 * Creates a function that is restricted to invoking `fn` once.
 * Repeat calls return the value from the first invocation.
 * 
 * @example
 * const initialize = once(() => expensiveOperation());
 * initialize(); // runs expensiveOperation
 * initialize(); // returns cached result
 */
export function once<T>(fn: () => T): () => T {
    let called = false;
    let result: T;
    return (): T => {
        if (!called) {
            called = true;
            result = fn();
        }
        return result;
    };
}
