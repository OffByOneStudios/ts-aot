// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: min: 50
// OUTPUT: max: 95

// Bug: Math.min/max with boxed pointer arguments caused LLVM compile error:
// "icmp sgt ptr vs i64" - type mismatch because arguments weren't unboxed.

function user_main(): number {
    const size: number = 100;
    let written: number = 5;

    // This pattern caused the bug - subtraction produces boxed value
    const remaining = size - written;
    const chunkSize = 50;

    const toWrite = Math.min(chunkSize, remaining);
    console.log("min: " + toWrite);

    const bigger = Math.max(chunkSize, remaining);
    console.log("max: " + bigger);

    return 0;
}
