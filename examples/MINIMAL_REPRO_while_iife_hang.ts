// MINIMAL REPRODUCTION CASE FOR WHILE LOOP HANG IN IIFE
//
// This is the simplest test case that demonstrates the bug:
// While loops inside IIFEs hang infinitely at runtime.
//
// Expected: Should print "Before" "Done: 10" "After"
// Actual: Prints "Before" then hangs forever

console.log("Before");

var result = (function() {
    console.log("IIFE: Start");
    var i = 0;
    var sum = 0;
    console.log("IIFE: Before while, i=", i);
    while (i < 5) {
        console.log("IIFE: In loop, i=", i, "sum=", sum);
        sum = sum + i;
        i = i + 1;
    }
    console.log("IIFE: After while, sum=", sum);
    return sum;
})();

console.log("Done:", result);
console.log("After");

// NOTES FOR DEBUGGING:
// 1. The IR generated is CORRECT (see docs/debug_reports/lodash_hang_analysis.md)
// 2. The same while loop works fine outside an IIFE
// 3. The hang is at RUNTIME, not compile-time
// 4. Check ts_call_0 runtime function
// 5. Check LLVM optimization level (-O0 vs -O2)
