// Split an array into chunks of the given size
export function chunk<T>(array: T[], size: number): T[][] {
    const result: T[][] = [];
    let i = 0;
    while (i < array.length) {
        const chunk: T[] = [];
        let j = 0;
        while (j < size && i < array.length) {
            chunk.push(array[i]);
            i = i + 1;
            j = j + 1;
        }
        result.push(chunk);
    }
    return result;
}
