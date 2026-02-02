// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// OUTPUT: sum1: 5.5
// OUTPUT: sum2: 5.5
// OUTPUT: elapsed_typed: 3
// OUTPUT: elapsed_untyped: 1

// Regression test: All numbers are now correctly treated as doubles (JavaScript semantics).
// Previously, `let x = 0` was inferred as integer, causing truncation.
// Now both typed and untyped declarations work correctly with floating point.

function user_main(): number {
    // Test 1: Basic double accumulation with explicit type
    let sum1: number = 0;
    sum1 = sum1 + 1.5;
    sum1 = sum1 + 2.0;
    sum1 = sum1 + 2.0;
    console.log("sum1: " + sum1);

    // Test 2: Same accumulation without type annotation (now works correctly)
    let sum2 = 0;
    sum2 = sum2 + 1.5;
    sum2 = sum2 + 2.0;
    sum2 = sum2 + 2.0;
    console.log("sum2: " + sum2);  // 5.5 - correct double semantics

    // Test 3: Simulating benchmark timing accumulation
    // This is the pattern that was broken in the benchmark harness
    let elapsed_typed: number = 0;
    elapsed_typed = elapsed_typed + 1.0;
    elapsed_typed = elapsed_typed + 1.0;
    elapsed_typed = elapsed_typed + 1.0;
    console.log("elapsed_typed: " + elapsed_typed);

    // Test 4: Without explicit type, accumulation still works (now fixed)
    let elapsed_untyped = 0;
    elapsed_untyped = elapsed_untyped + 0.5;
    elapsed_untyped = elapsed_untyped + 0.3;
    elapsed_untyped = elapsed_untyped + 0.2;
    console.log("elapsed_untyped: " + elapsed_untyped);  // 1 - correct

    return 0;
}
