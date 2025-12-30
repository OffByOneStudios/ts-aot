// Remove falsy values from array
export function compact<T>(array: T[]): T[] {
    const result: T[] = [];
    for (const item of array) {
        if (item) {
            result.push(item);
        }
    }
    return result;
}
