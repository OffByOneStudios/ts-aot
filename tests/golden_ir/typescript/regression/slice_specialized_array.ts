// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_array_slice
// OUTPUT: slice1: 20,30
// OUTPUT: slice2: 30,40,50
// OUTPUT: first: 20
// OUTPUT: last: 50

// Regression test: TsArray::Slice was not preserving isSpecialized and isDouble
// flags when creating the new sliced array. This caused element access on the
// sliced array to crash because the runtime tried to unbox raw double bits as
// TsValue pointers.

function user_main(): number {
    // Create a specialized number[] array (isSpecialized=true, isDouble=true)
    const arr: number[] = [10, 20, 30, 40, 50];

    // Slice the array - this must preserve the specialized flags
    const slice1 = arr.slice(1, 3);
    const slice2 = arr.slice(2, 5);

    // Output the slices
    console.log("slice1: " + slice1.join(","));
    console.log("slice2: " + slice2.join(","));

    // Critical test: access individual elements of the sliced array
    // This was the operation that crashed before the fix
    const first = slice1[0];
    const last = slice2[2];

    console.log("first: " + first);
    console.log("last: " + last);

    return 0;
}
