// Simple sort test with actual sorting

function simpleSortBy<T>(array: T[], iteratee: (value: T) => number): T[] {
    const result: T[] = [];
    let i = 0;
    const len = array.length;
    while (i < len) {
        result.push(array[i]);
        i = i + 1;
    }
    
    // Now sort using the iteratee
    result.sort((a: T, b: T) => {
        const aKey = iteratee(a);
        const bKey = iteratee(b);
        if (aKey < bKey) return -1;
        if (aKey > bKey) return 1;
        return 0;
    });
    
    return result;
}

console.log("Testing simple sortBy:");
const nums = [3, 1, 4, 1, 5];
const sorted = simpleSortBy(nums, (n: number) => n);
console.log("Length:");
console.log(sorted.length);
console.log("First (should be 1):");
console.log(sorted[0]);

console.log("Done!");
