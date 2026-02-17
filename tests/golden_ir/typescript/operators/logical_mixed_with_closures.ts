// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: Host: 0.0.0.0
// OUTPUT: Handler result: GET /api
// OUTPUT: AND value: result
// OUTPUT: AND handler result: 10
// OUTPUT: All logical+closure tests passed!

// Test: Logical operators followed by arrow function definitions
// Regression test for chat-server crash: || or && creates HIR blocks,
// then arrow function definition saves/restores currentBlock_.
// Without the currentBlock_ sync fix, DCE removes the merge blocks
// as unreachable because the entry block's terminator becomes Return
// instead of CondBranch.

function user_main(): number {
    // Test 1: || followed by arrow function (exact chat-server pattern)
    const HOST: string = "" || "0.0.0.0";
    const handler = (method: string, path: string): string => {
        return method + " " + path;
    };
    console.log("Host: " + HOST);
    console.log("Handler result: " + handler("GET", "/api"));

    // Test 2: && followed by arrow function
    const val: string = "truthy" && "result";
    const transform = (x: number): number => {
        return x + 5;
    };
    console.log("AND value: " + val);
    console.log("AND handler result: " + transform(5));

    console.log("All logical+closure tests passed!");
    return 0;
}
