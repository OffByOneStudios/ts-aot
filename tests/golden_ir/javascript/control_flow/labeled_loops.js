// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// Test: Labeled loops with break and continue (JS slow path)
// CHECK: define
// OUTPUT: 0,0
// OUTPUT: 1,0
// OUTPUT: 2,0
// OUTPUT: done

// break outer from inner loop
outer: for (var i = 0; i < 5; i++) {
    for (var j = 0; j < 5; j++) {
        if (i === 3) {
            break outer;
        }
        if (j > 0) {
            continue;
        }
        console.log(i + "," + j);
    }
}
console.log("done");
