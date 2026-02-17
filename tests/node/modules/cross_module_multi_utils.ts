// Library file: utility functions for multi-module testing

export function formatName(first: string, last: string): string {
    return first + " " + last;
}

export function generateId(): string {
    return Math.random().toString(36).substring(2);
}

export function capitalize(s: string): string {
    if (s.length === 0) return s;
    return s.charAt(0).toUpperCase() + s.substring(1);
}
