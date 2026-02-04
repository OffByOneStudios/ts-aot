// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: PASS

// Test computed enum members
enum ComputedEnum {
    A = 1 + 2,           // Computed: 3
    B = "hello".length,  // Computed: 5
    C = Math.floor(3.7), // Computed: 3
    D = A * 2,           // Computed using another member: 6
}

function user_main(): number {
    // Verify computed values
    if (ComputedEnum.A !== 3) return 1;
    if (ComputedEnum.B !== 5) return 1;
    if (ComputedEnum.C !== 3) return 1;
    if (ComputedEnum.D !== 6) return 1;
    
    console.log("PASS");
    return 0;
}
