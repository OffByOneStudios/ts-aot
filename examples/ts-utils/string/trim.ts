// Trim whitespace from both ends
export function trim(str: string): string {
    return str.trim();
}

// Trim whitespace from start
export function trimStart(str: string): string {
    let i = 0;
    while (i < str.length && str.charAt(i) === ' ') {
        i = i + 1;
    }
    return str.slice(i);
}

// Trim whitespace from end
export function trimEnd(str: string): string {
    let i = str.length;
    while (i > 0 && str.charAt(i - 1) === ' ') {
        i = i - 1;
    }
    return str.slice(0, i);
}
