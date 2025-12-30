// Bug: Nested loop variable scoping issue
// Variables declared in nested while loops get incorrect LLVM types (ptr vs i64)

function testNestedLoop(): number {
    let i = 0;
    let total = 0;
    
    while (i < 3) {
        let j = 0;  // This 'j' gets ptr type instead of i64 in inner loop comparisons
        while (j < 2) {
            total = total + 1;
            j = j + 1;
        }
        i = i + 1;
    }
    
    return total;  // Should be 6 (3 outer * 2 inner)
}

console.log(testNestedLoop());
