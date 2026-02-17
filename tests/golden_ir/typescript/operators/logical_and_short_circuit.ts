// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: Basic AND: result
// OUTPUT: AND falsy: [empty]
// OUTPUT: AND with callback: result
// OUTPUT: Callback result: 10
// OUTPUT: AND chain: last
// OUTPUT: AND chain falsy: [empty]
// OUTPUT: All logical AND tests passed!

// Test: Logical AND (&&) short-circuit semantics
// Regression test: && with arrow function callbacks caused same bug as ||

function user_main(): number {
    // Test 1: Basic && with truthy LHS
    const a: string = "truthy" && "result";
    console.log("Basic AND: " + a);

    // Test 2: && with falsy LHS (should NOT evaluate RHS)
    const b: string = "" && "never";
    console.log("AND falsy: " + (b || "[empty]"));

    // Test 3: && followed by arrow function definition (regression pattern)
    const val: string = "truthy" && "result";
    const fn = (x: number): number => {
        return x + 5;
    };
    console.log("AND with callback: " + val);
    console.log("Callback result: " + fn(5));

    // Test 4: Chained && operators
    const c: string = "first" && "second" && "last";
    console.log("AND chain: " + c);

    const d: string = "" && "second" && "last";
    console.log("AND chain falsy: " + (d || "[empty]"));

    console.log("All logical AND tests passed!");
    return 0;
}
