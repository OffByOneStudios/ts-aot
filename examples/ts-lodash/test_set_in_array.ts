// Test Set in array
function testSetArray(...arrays: number[][]): void {
    console.log("Arrays received:");
    console.log(arrays.length);
    
    // Build Sets array
    const otherSets: Set<number>[] = [];
    
    // Add a Set for arrays[1]
    const s = new Set<number>();
    const arr = arrays[1];
    let i = 0;
    while (i < arr.length) {
        s.add(arr[i]);
        i = i + 1;
    }
    console.log("Set size before push:");
    console.log(s.size);
    
    otherSets.push(s);
    
    console.log("otherSets.length:");
    console.log(otherSets.length);
    
    // Get the set back from array
    const setFromArray = otherSets[0];
    console.log("setFromArray.size:");
    console.log(setFromArray.size);
    
    // Check if we can use has on the retrieved set
    const first = arrays[0];
    let j = 0;
    while (j < first.length) {
        const val = first[j];
        console.log("Checking val:");
        console.log(val);
        const hasIt = setFromArray.has(val);
        console.log("Has it:");
        console.log(hasIt);
        j = j + 1;
    }
}

testSetArray([1, 2, 3], [2, 3, 4]);
