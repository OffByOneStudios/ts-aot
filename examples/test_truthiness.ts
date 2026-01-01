// Test JavaScript truthiness semantics

console.log("=== Truthiness Test ===");

// Test with array filter using truthiness
let mixedArr: any[] = [0, 1, "", "hello", false, true, null, undefined];

// Filter using Boolean as callback would require truthiness checks
let i = 0;
for (i = 0; i < mixedArr.length; i++) {
    let val: any = mixedArr[i];
    if (val) {
        console.log("truthy:");
        console.log(val);
    } else {
        console.log("falsy:");
        console.log(val);
    }
}

console.log("=== Test Complete ===");
