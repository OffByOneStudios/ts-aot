/**
 * Converts the first character of string to upper case and the remaining to lower case.
 * 
 * @example
 * capitalize('FRED') // => 'Fred'
 * capitalize('hELLO') // => 'Hello'
 */
export function capitalize(str: string): string {
    if (str.length === 0) return str;
    const first = str.charAt(0).toUpperCase();
    const rest = str.slice(1, str.length).toLowerCase();
    return first + rest;
}
