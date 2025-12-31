/**
 * Truncates string if it's longer than the given maximum string length.
 * The last characters of the truncated string are replaced with the omission string.
 * 
 * @example
 * truncate('hi-diddly-ho there, neighborino', 24) // => 'hi-diddly-ho there, n...'
 */
export function truncate(str: string, length: number, omission: string): string {
    if (str.length <= length) return str;
    
    const end = length - omission.length;
    if (end < 0) return omission.slice(0, length);
    
    return str.slice(0, end) + omission;
}
