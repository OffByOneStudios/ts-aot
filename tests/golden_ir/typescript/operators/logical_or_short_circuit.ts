// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: Basic OR: fallback
// OUTPUT: OR truthy: hello
// OUTPUT: OR with callback: fallback
// OUTPUT: Callback result: 42
// OUTPUT: OR chain: first
// OUTPUT: OR chain fallback: last
// OUTPUT: All logical OR tests passed!

// Test: Logical OR (||) short-circuit semantics
// Regression test: || with arrow function callbacks caused DCE to remove blocks
// because ASTToHIR::currentBlock_ was not synced with builder after setInsertPoint

function user_main(): number {
    // Test 1: Basic || with falsy LHS
    const a: string = "" || "fallback";
    console.log("Basic OR: " + a);

    // Test 2: || with truthy LHS (should NOT evaluate RHS)
    const b: string = "hello" || "world";
    console.log("OR truthy: " + b);

    // Test 3: || followed by arrow function definition (the regression pattern)
    // This is the exact pattern that caused the chat-server crash:
    // The || creates HIR blocks, then the arrow function saves/restores currentBlock_
    const host: string = "" || "fallback";
    const fn = (x: number): number => {
        return x * 2;
    };
    console.log("OR with callback: " + host);
    console.log("Callback result: " + fn(21));

    // Test 4: Chained || operators
    const c: string = "first" || "second" || "third";
    console.log("OR chain: " + c);

    const d: string = "" || "" || "last";
    console.log("OR chain fallback: " + d);

    console.log("All logical OR tests passed!");
    return 0;
}
