function test(...arrays: number[][]): void {
    const inner = arrays[1];
    for (const item of inner) {
        console.log("item:", item);
    }
}

test([1, 2, 3], [2, 3, 4], [3, 4, 5]);
