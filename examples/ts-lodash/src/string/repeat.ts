/**
 * Repeats the given string n times.
 * 
 * @example
 * repeat('*', 3) // => '***'
 * repeat('abc', 2) // => 'abcabc'
 */
export function repeat(str: string, n: number): string {
    if (n <= 0) return "";
    return str.repeat(n);
}
