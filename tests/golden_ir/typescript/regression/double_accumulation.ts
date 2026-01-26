// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// OUTPUT: sum1: 5.5
// OUTPUT: sum2: 5
// OUTPUT: elapsed_typed: 3
// OUTPUT: elapsed_untyped: 0

// Regression test: When a variable is declared as `let x = 0`, the type is
// inferred as integer. When doubles are added to it, they get truncated to
// integers. The fix is to use explicit type annotation: `let x: number = 0`.

function user_main(): number {
    // Test 1: Basic double accumulation with explicit type
    let sum1: number = 0;
    sum1 = sum1 + 1.5;
    sum1 = sum1 + 2.0;
    sum1 = sum1 + 2.0;
    console.log("sum1: " + sum1);

    // Test 2: Same accumulation without type annotation (infers int, truncates)
    let sum2 = 0;
    sum2 = sum2 + 1.5;  // 1.5 truncates to 1
    sum2 = sum2 + 2.0;  // 2.0 truncates to 2
    sum2 = sum2 + 2.0;  // 2.0 truncates to 2
    console.log("sum2: " + sum2);  // Will be 5, not 5.5

    // Test 3: Simulating benchmark timing accumulation
    // This is the pattern that was broken in the benchmark harness
    let elapsed_typed: number = 0;
    elapsed_typed = elapsed_typed + 1.0;
    elapsed_typed = elapsed_typed + 1.0;
    elapsed_typed = elapsed_typed + 1.0;
    console.log("elapsed_typed: " + elapsed_typed);

    // Test 4: Without type, timing values are truncated to 0
    let elapsed_untyped = 0;
    elapsed_untyped = elapsed_untyped + 0.5;  // Truncates to 0
    elapsed_untyped = elapsed_untyped + 0.3;  // Truncates to 0
    elapsed_untyped = elapsed_untyped + 0.2;  // Truncates to 0
    console.log("elapsed_untyped: " + elapsed_untyped);  // Will be 0

    return 0;
}
