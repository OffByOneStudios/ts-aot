// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: Testing triple-slash directives...
// OUTPUT: Reference path: parsed
// OUTPUT: Reference types: parsed
// OUTPUT: Reference lib: parsed
// OUTPUT: All directive tests passed!

// Test: Triple-slash directives are parsed without errors
// Note: These directives are parsed and stored in AST but not resolved to files

/// <reference path="./nonexistent.ts" />
/// <reference types="node" />
/// <reference lib="es2020" />

function user_main(): number {
    console.log("Testing triple-slash directives...");

    // The directives above should be parsed without compilation errors
    // They don't affect runtime behavior in this AOT compiler

    console.log("Reference path: parsed");
    console.log("Reference types: parsed");
    console.log("Reference lib: parsed");

    console.log("All directive tests passed!");
    return 0;
}
