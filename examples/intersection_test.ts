function intersection<T>(...arrays: T[][]): T[] {
    console.log("arrays.length:", arrays.length);
    if (arrays.length === 0) return [];
    console.log("arrays[0].length:", arrays[0].length);
    if (arrays.length === 1) return arrays[0];
    
    // Use Set for faster lookup
    const sets: Set<T>[] = [];
    for (let i = 1; i < arrays.length; i++) {
        console.log("Building set for array", i, "length:", arrays[i].length);
        const s = new Set<T>();
        for (const item of arrays[i]) {
            console.log("  adding:", item);
            s.add(item);
        }
        console.log("  set.size:", s.size);
        sets.push(s);
    }
    
    console.log("sets.length:", sets.length);
    const result: T[] = [];
    for (const item of arrays[0]) {
        console.log("Checking item:", item);
        let inAll = true;
        for (let j = 0; j < sets.length; j++) {
            const s = sets[j];
            console.log("  s.has(item):", s.has(item));
            if (!s.has(item)) {
                inAll = false;
                break;
            }
        }
        if (inAll) {
            console.log("  -> adding to result");
            result.push(item);
        }
    }
    return result;
}

const result = intersection([1, 2, 3], [2, 3, 4], [3, 4, 5]);
console.log("intersection result:", result.length);
