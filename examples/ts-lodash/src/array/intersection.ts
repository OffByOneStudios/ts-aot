/**
 * Creates an array of unique values that are included in all given arrays.
 * 
 * @example
 * intersection([2, 1], [2, 3]) // => [2]
 * intersection([1, 2, 3], [2, 3, 4], [3, 4, 5]) // => [3]
 */
export function intersection<T>(...arrays: T[][]): T[] {
    if (arrays.length === 0) {
        return [];
    }
    
    // Use first array as base
    const first = arrays[0];
    if (arrays.length === 1) {
        return uniq(first);
    }
    
    // Build Sets for all other arrays
    // Note: Can't use new Set(array) as constructor args aren't fully supported
    const otherSets: Set<T>[] = [];
    let i = 1;
    while (i < arrays.length) {
        const s = new Set<T>();
        const arr = arrays[i];
        let idx = 0;
        while (idx < arr.length) {
            s.add(arr[idx]);
            idx = idx + 1;
        }
        otherSets.push(s);
        i = i + 1;
    }
    
    // Find values in first array that exist in all other arrays
    const result: T[] = [];
    const seen = new Set<T>();
    
    let j = 0;
    while (j < first.length) {
        const value = first[j];
        
        // Skip if already seen (ensure uniqueness)
        if (!seen.has(value)) {
            seen.add(value);
            
            // Check if value exists in all other arrays
            let inAll = true;
            let k = 0;
            while (k < otherSets.length) {
                if (!otherSets[k].has(value)) {
                    inAll = false;
                    k = otherSets.length; // break
                }
                k = k + 1;
            }
            
            if (inAll) {
                result.push(value);
            }
        }
        
        j = j + 1;
    }
    
    return result;
}

// Helper function for uniqueness
function uniq<T>(array: T[]): T[] {
    const result: T[] = [];
    const seen = new Set<T>();
    
    let i = 0;
    while (i < array.length) {
        const value = array[i];
        if (!seen.has(value)) {
            seen.add(value);
            result.push(value);
        }
        i = i + 1;
    }
    
    return result;
}
