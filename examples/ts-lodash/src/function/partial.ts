/**
 * Partially applies arguments to a 2-argument function.
 * Returns a function that takes the remaining argument.
 * 
 * @example
 * const add = (a: number, b: number) => a + b;
 * const add5 = partial1_2(add, 5);
 * add5(3); // 8
 */
export function partial1_2(fn: (a: number, b: number) => number, arg1: number): (b: number) => number {
    return (b: number): number => fn(arg1, b);
}

/**
 * Partially applies first two arguments to a 3-argument function.
 * Returns a function that takes the remaining argument.
 * 
 * @example
 * const multiply3 = (a: number, b: number, c: number) => a * b * c;
 * const mult6 = partial2_3(multiply3, 2, 3);
 * mult6(4); // 24
 */
export function partial2_3(fn: (a: number, b: number, c: number) => number, arg1: number, arg2: number): (c: number) => number {
    return (c: number): number => fn(arg1, arg2, c);
}
