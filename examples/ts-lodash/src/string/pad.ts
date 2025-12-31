/**
 * Pads string on the left and right sides if it's shorter than length.
 * 
 * @example
 * pad('abc', 8) // => '  abc   '
 * pad('abc', 8, '_-') // => '_-abc_-_'
 */
export function pad(str: string, length: number, chars: string): string {
    const strLen = str.length;
    if (strLen >= length) return str;
    
    const padLen = length - strLen;
    const leftPad = Math.floor(padLen / 2);
    const rightPad = padLen - leftPad;
    
    // Inline padding creation
    let leftResult = "";
    let leftI = 0;
    const leftChars = chars.length > 0 ? chars : " ";
    while (leftResult.length < leftPad) {
        leftResult = leftResult + leftChars.charAt(leftI % leftChars.length);
        leftI = leftI + 1;
    }
    leftResult = leftResult.slice(0, leftPad);
    
    let rightResult = "";
    let rightI = 0;
    while (rightResult.length < rightPad) {
        rightResult = rightResult + leftChars.charAt(rightI % leftChars.length);
        rightI = rightI + 1;
    }
    rightResult = rightResult.slice(0, rightPad);
    
    return leftResult + str + rightResult;
}

/**
 * Pads string on the left side if it's shorter than length.
 * 
 * @example
 * padStart('abc', 6) // => '   abc'
 * padStart('abc', 6, '_-') // => '_-_abc'
 */
export function padStart(str: string, length: number, chars: string): string {
    const strLen = str.length;
    if (strLen >= length) return str;
    
    const padLen = length - strLen;
    const padChars = chars.length > 0 ? chars : " ";
    
    let result = "";
    let i = 0;
    while (result.length < padLen) {
        result = result + padChars.charAt(i % padChars.length);
        i = i + 1;
    }
    
    return result.slice(0, padLen) + str;
}

/**
 * Pads string on the right side if it's shorter than length.
 * 
 * @example
 * padEnd('abc', 6) // => 'abc   '
 * padEnd('abc', 6, '_-') // => 'abc_-_'
 */
export function padEnd(str: string, length: number, chars: string): string {
    const strLen = str.length;
    if (strLen >= length) return str;
    
    const padLen = length - strLen;
    const padChars = chars.length > 0 ? chars : " ";
    
    let result = "";
    let i = 0;
    while (result.length < padLen) {
        result = result + padChars.charAt(i % padChars.length);
        i = i + 1;
    }
    
    return str + result.slice(0, padLen);
}
