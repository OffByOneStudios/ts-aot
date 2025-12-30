// Exported generic functions for testing cross-module generic imports

export function identity<T>(x: T): T {
    return x;
}

export function first<T>(arr: T[]): T {
    return arr[0];
}

export function map<T, U>(arr: T[], fn: (x: T) => U): U[] {
    const result: U[] = [];
    for (let i = 0; i < arr.length; i++) {
        result.push(fn(arr[i]));
    }
    return result;
}
