// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: both true: yes
// OUTPUT: one false: no
// OUTPUT: either true: yes
// OUTPUT: both false: no
// OUTPUT: not true: false
// OUTPUT: not false: true

// Test: Boolean captures used in logical AND, OR, NOT expressions
function makeAndChecker(a: boolean, b: boolean): () => string {
    return (): string => {
        if (a === true && b === true) {
            return "yes";
        }
        return "no";
    };
}

function makeOrChecker(a: boolean, b: boolean): () => string {
    return (): string => {
        if (a === true || b === true) {
            return "yes";
        }
        return "no";
    };
}

function makeNotChecker(val: boolean): () => string {
    return (): string => {
        if (val === false) {
            return "true";
        }
        return "false";
    };
}

console.log("both true: " + makeAndChecker(true, true)());
console.log("one false: " + makeAndChecker(true, false)());
console.log("either true: " + makeOrChecker(false, true)());
console.log("both false: " + makeOrChecker(false, false)());
console.log("not true: " + makeNotChecker(true)());
console.log("not false: " + makeNotChecker(false)());
