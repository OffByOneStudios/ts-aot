/**
 * Converts string to snake_case.
 * 
 * @example
 * snakeCase('Foo Bar') // => 'foo_bar'
 * snakeCase('fooBar') // => 'foo_bar'
 * snakeCase('--FOO-BAR--') // => 'foo_bar'
 */
export function snakeCase(str: string): string {
    // Inline word splitting logic
    const words: string[] = [];
    let current = "";
    let i = 0;
    
    while (i < str.length) {
        const code = str.charCodeAt(i);
        const c = str.charAt(i);
        
        const isAlpha = (code >= 65 && code <= 90) || (code >= 97 && code <= 122);
        const isDigit = code >= 48 && code <= 57;
        const isUpper = code >= 65 && code <= 90;
        
        if (isAlpha || isDigit) {
            if (current.length > 0 && isUpper && i > 0) {
                const prevCode = str.charCodeAt(i - 1);
                const prevIsLower = prevCode >= 97 && prevCode <= 122;
                if (prevIsLower) {
                    words.push(current);
                    current = "";
                }
            }
            current = current + c;
        } else {
            if (current.length > 0) {
                words.push(current);
                current = "";
            }
        }
        i = i + 1;
    }
    
    if (current.length > 0) {
        words.push(current);
    }
    
    // Build snake_case result
    if (words.length === 0) return "";
    
    let result = words[0].toLowerCase();
    let j = 1;
    while (j < words.length) {
        const word = words[j];
        if (word.length > 0) {
            result = result + "_" + word.toLowerCase();
        }
        j = j + 1;
    }
    return result;
}
