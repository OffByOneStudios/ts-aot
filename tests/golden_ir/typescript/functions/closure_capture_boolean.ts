// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// CHECK: @ts_cell_get
// OUTPUT: flag is true
// OUTPUT: flag is false
// OUTPUT: done

// Test: Boolean variable captured by closure, used in if/else conditional
function makeFlagChecker(initial: boolean): () => string {
    let flag: boolean = initial;
    return (): string => {
        if (flag === true) {
            return "flag is true";
        } else {
            return "flag is false";
        }
    };
}

const checkTrue = makeFlagChecker(true);
console.log(checkTrue());

const checkFalse = makeFlagChecker(false);
console.log(checkFalse());

console.log("done");
