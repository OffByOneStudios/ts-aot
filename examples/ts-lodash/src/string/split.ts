/**
 * Splits string by separator.
 * 
 * @example
 * split('a-b-c', '-') // => ['a', 'b', 'c']
 */
export function split(str: string, separator: string): string[] {
    return str.split(separator);
}
