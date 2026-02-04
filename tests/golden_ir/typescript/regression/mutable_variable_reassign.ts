// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// Test: Mutable variable reassignment in switch block
// 
// Bug: The compiler loses track of mutable variables when they are
// reassigned inside block scopes (switch cases, for loop bodies, etc.)
// and reports "Error: Unknown variable name <var>"

const x = 2;
let result = 0;

switch (x) {
    case 1:
        result = 10;
        break;
    case 2:
        result = 20;
        break;
    default:
        result = 30;
        break;
}

console.log(result);

// OUTPUT: 20
// CHECK: define
// CHECK: fptosi
// CHECK: switch i64
// CHECK: switch.end
