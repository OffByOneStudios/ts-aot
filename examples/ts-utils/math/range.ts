// Generate a range of numbers from start to end (exclusive)
export function range(start: number, end: number): number[] {
    const result: number[] = [];
    let i = start;
    while (i < end) {
        result.push(i);
        i = i + 1;
    }
    return result;
}
