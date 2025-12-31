/**
 * Removes leading and trailing whitespace or specified characters from string.
 * 
 * @example
 * trim('  abc  ') // => 'abc'
 */
export function trim(str: string): string {
    return str.trim();
}

/**
 * Removes leading whitespace or specified characters from string.
 * 
 * @example
 * trimStart('  abc  ') // => 'abc  '
 */
export function trimStart(str: string): string {
    let i = 0;
    while (i < str.length && str.charAt(i) === " ") {
        i = i + 1;
    }
    return str.slice(i, str.length);
}

/**
 * Removes trailing whitespace or specified characters from string.
 * 
 * @example
 * trimEnd('  abc  ') // => '  abc'
 */
export function trimEnd(str: string): string {
    let i = str.length;
    while (i > 0 && str.charAt(i - 1) === " ") {
        i = i - 1;
    }
    return str.slice(0, i);
}
