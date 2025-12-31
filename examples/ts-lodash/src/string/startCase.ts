/**
 * Converts string to Start Case (title case).
 * 
 * @example
 * startCase('fooBar') // => 'Foo Bar'
 * startCase('--foo-bar--') // => 'Foo Bar'
 * startCase('__FOO_BAR__') // => 'FOO BAR'
 */
export function startCase(str: string): string {
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
    
    // Build Start Case result
    if (words.length === 0) return "";
    
    // Capitalize first word
    const firstWord = words[0];
    let result = firstWord.charAt(0).toUpperCase() + firstWord.slice(1, firstWord.length);
    
    let j = 1;
    while (j < words.length) {
        const word = words[j];
        if (word.length > 0) {
            result = result + " " + word.charAt(0).toUpperCase() + word.slice(1, word.length);
        }
        j = j + 1;
    }
    return result;
}
