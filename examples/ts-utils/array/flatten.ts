// Flatten a nested array one level deep
export function flatten<T>(array: T[][]): T[] {
    const result: T[] = [];
    for (const inner of array) {
        for (const item of inner) {
            result.push(item);
        }
    }
    return result;
}
