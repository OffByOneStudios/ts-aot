// Return unique values from array
export function uniq<T>(array: T[]): T[] {
    const seen = new Set<T>();
    const result: T[] = [];
    for (const item of array) {
        if (!seen.has(item)) {
            seen.add(item);
            result.push(item);
        }
    }
    return result;
}
