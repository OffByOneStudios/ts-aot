// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// OUTPUT: arr[0]: 100
// OUTPUT: arr[1]: 99
// OUTPUT: arr[2]: 98
// OUTPUT: sum: 5050

// Bug: When integers are pushed to a number[] array, they are stored correctly
// but read back as garbage values (tiny denormalized doubles like 4.94066e-322).
//
// This suggests the specialized number array stores values as int64 but reads
// them back interpreting the raw bits as double, or vice versa.
//
// Current broken output:
//   arr[0]: 4.94066e-322  (should be 100)
//   arr[1]: 4.89125e-322  (should be 99)

function user_main(): number {
    // Create array by pushing integers
    const arr: number[] = [];
    for (let i = 100; i > 0; i--) {
        arr.push(i);
    }

    // Read back - should be integers, not denormalized floats
    console.log("arr[0]: " + arr[0]);
    console.log("arr[1]: " + arr[1]);
    console.log("arr[2]: " + arr[2]);

    // Calculate sum to verify values
    let sum: number = 0;
    for (let i = 0; i < arr.length; i++) {
        sum = sum + arr[i];
    }
    console.log("sum: " + sum);  // Should be 5050

    return 0;
}
