// Pad string on left to target length
export function padStart(str: string, length: number, char: string): string {
    let result = str;
    while (result.length < length) {
        result = char + result;
    }
    return result;
}

// Pad string on right to target length
export function padEnd(str: string, length: number, char: string): string {
    let result = str;
    while (result.length < length) {
        result = result + char;
    }
    return result;
}
