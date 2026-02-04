// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: start_valid: true
// OUTPUT: elapsed_positive: true
// OUTPUT: measurements: 5

function user_main(): number {
    // Test performance.now()
    const start = performance.now();
    console.log("start_valid: " + (start >= 0));

    // Do some work
    let sum = 0;
    for (let i = 0; i < 1000000; i++) {
        sum = sum + i;
    }

    const elapsed = performance.now() - start;
    console.log("elapsed_positive: " + (elapsed > 0));

    // Test array.push() with numbers
    const measurements: number[] = [];
    for (let i = 0; i < 5; i++) {
        const t = performance.now();
        measurements.push(t);
    }
    console.log("measurements: " + measurements.length);

    return 0;
}
