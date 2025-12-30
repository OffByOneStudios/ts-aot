// Debug intersection
function intersection(...arrays: number[][]): number[] {
    console.log("In intersection");
    console.log(arrays.length);
    
    if (arrays.length === 0) {
        console.log("Empty arrays");
        return [];
    }
    
    const first = arrays[0];
    console.log("First array length:");
    console.log(first.length);
    
    if (arrays.length === 1) {
        console.log("Only one array");
        return first;
    }
    
    // Build Sets for all other arrays
    const otherSets: Set<number>[] = [];
    let i = 1;
    while (i < arrays.length) {
        const s = new Set<number>();
        const arr = arrays[i];
        console.log("Processing arr, length:");
        console.log(arr.length);
        let idx = 0;
        while (idx < arr.length) {
            console.log("Adding to set:");
            console.log(arr[idx]);
            s.add(arr[idx]);
            idx = idx + 1;
        }
        console.log("Set size:");
        console.log(s.size);
        otherSets.push(s);
        i = i + 1;
    }
    
    console.log("Built otherSets, count:");
    console.log(otherSets.length);
    
    // Find values in first array that exist in all other arrays
    const result: number[] = [];
    const seen = new Set<number>();
    
    let j = 0;
    while (j < first.length) {
        const value = first[j];
        console.log("Checking value:");
        console.log(value);
        
        if (!seen.has(value)) {
            seen.add(value);
            
            let inAll = true;
            let k = 0;
            while (k < otherSets.length) {
                console.log("Checking in set k:");
                console.log(k);
                const hasIt = otherSets[k].has(value);
                console.log("Has it?");
                console.log(hasIt);
                if (!hasIt) {
                    inAll = false;
                    k = otherSets.length; // break
                }
                k = k + 1;
            }
            
            if (inAll) {
                console.log("Value in all, adding to result");
                result.push(value);
            }
        }
        
        j = j + 1;
    }
    
    console.log("Result length:");
    console.log(result.length);
    return result;
}

const r = intersection([1, 2, 3], [2, 3, 4], [3, 4, 5]);
console.log("Final result:");
console.log(r.length);
