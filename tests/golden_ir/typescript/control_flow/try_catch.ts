// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// Test: Try-catch-finally exception handling
// OUTPUT: caught
// OUTPUT: finally

try {
    throw new Error("test");
} catch (e) {
    console.log("caught");
} finally {
    console.log("finally");
}
