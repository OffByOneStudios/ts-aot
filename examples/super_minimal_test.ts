// Super minimal test - just access arrays[0] and print it

function test(...arrays: number[][]): number {
    console.log("arrays.length:");
    console.log(arrays.length);  // Should print 3
    
    console.log("Getting arrays[0]...");
    const first = arrays[0];
    
    console.log("Got first, accessing first.length...");
    console.log(first.length);  // Should print 3
    
    console.log("Accessing first[0]...");
    const val = first[0];
    console.log(val);  // Should print 1
    
    return 1;
}

const result = test([1, 2, 3], [2, 3, 4], [3, 4, 5]);
console.log("Done:");
console.log(result);
