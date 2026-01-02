/**
 * Curries a 2-argument function.
 * Returns a chain of single-argument functions.
 * 
 * @example
 * const add = (a: number, b: number) => a + b;
 * const curriedAdd = curry2(add);
 * curriedAdd(3)(4); // 7
 * 
 * const add10 = curriedAdd(10);
 * add10(5); // 15
 */
export function curry2(fn: (a: number, b: number) => number): (a: number) => (b: number) => number {
    return (a: number): ((b: number) => number) => {
        return (b: number): number => fn(a, b);
    };
}

// NOTE: curry3 is not supported due to triple-nested closure bug
// See: examples/test_lodash.ts for details
