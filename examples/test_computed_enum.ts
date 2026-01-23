// Test computed enum members

enum ComputedEnum {
    A = 1 + 2,           // Computed: 3
    B = "hello".length,  // Computed: 5
    C = Math.floor(3.7), // Computed: 3
    D = A * 2,           // Computed using another member: 6
}

function user_main(): number {
    console.log("Testing computed enum members...");

    console.log("ComputedEnum.A = " + ComputedEnum.A);
    console.log("ComputedEnum.B = " + ComputedEnum.B);
    console.log("ComputedEnum.C = " + ComputedEnum.C);
    console.log("ComputedEnum.D = " + ComputedEnum.D);

    // Expected: A=3, B=5, C=3, D=6
    if (ComputedEnum.A === 3 && ComputedEnum.B === 5 && ComputedEnum.C === 3 && ComputedEnum.D === 6) {
        console.log("PASS: computed enum members work");
        return 0;
    } else {
        console.log("FAIL: computed enum member values incorrect");
        return 1;
    }
}
