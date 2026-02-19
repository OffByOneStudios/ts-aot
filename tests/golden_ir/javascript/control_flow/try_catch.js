// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: caught: test error
// OUTPUT: finally

try {
    throw new Error("test error");
} catch (e) {
    console.log("caught: " + e.message);
} finally {
    console.log("finally");
}
