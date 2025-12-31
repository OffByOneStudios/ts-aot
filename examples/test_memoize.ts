// Test for memoize function
function memoize(fn: (key: number) => number): (key: number) => number {
    const cache = new Map<number, number>();
    return (key: number): number => {
        if (cache.has(key)) {
            return cache.get(key) as number;
        }
        const result = fn(key);
        cache.set(key, result);
        return result;
    };
}

function square(x: number): number {
    console.log("  computing square of", x);
    return x * x;
}

const memoized = memoize(square);

console.log("Test memoize:");
console.log("First call memoized(5):", memoized(5));
console.log("Second call memoized(5) (cached):", memoized(5));
console.log("Third call memoized(3):", memoized(3));
