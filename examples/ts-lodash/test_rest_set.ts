// Test cross-array Set operations with rest params
function testRest(...arrays: number[][]): void {
    console.log("Arrays received:");
    console.log(arrays.length);
    
    // Build a Set from arrays[1]
    const s = new Set<number>();
    const arr = arrays[1];
    let i = 0;
    while (i < arr.length) {
        console.log("Adding to set:");
        console.log(arr[i]);
        s.add(arr[i]);
        i = i + 1;
    }
    
    console.log("Set size:");
    console.log(s.size);
    
    // Now check values from arrays[0]
    const first = arrays[0];
    let j = 0;
    while (j < first.length) {
        const val = first[j];
        console.log("Checking val:");
        console.log(val);
        const hasIt = s.has(val);
        console.log("Has it:");
        console.log(hasIt);
        j = j + 1;
    }
}

testRest([1, 2, 3], [2, 3, 4]);
