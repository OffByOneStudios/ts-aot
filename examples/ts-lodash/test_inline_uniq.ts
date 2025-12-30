// Inline uniq test without imports
function myUniq(array: number[]): number[] {
    const result: number[] = [];
    const seen = new Set<number>();
    
    let i = 0;
    const len = array.length;
    while (i < len) {
        const value = array[i];
        if (!seen.has(value)) {
            seen.add(value);
            result.push(value);
        }
        i = i + 1;
    }
    
    return result;
}

const arr = [1, 2, 1, 3, 2, 4];
const unique = myUniq(arr);
console.log(unique.length);
console.log("Done!");
