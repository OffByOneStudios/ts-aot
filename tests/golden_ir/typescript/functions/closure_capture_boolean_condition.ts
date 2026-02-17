// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: enabled
// OUTPUT: disabled
// OUTPUT: 1
// OUTPUT: 0

// Test: Boolean captured value used directly as a condition in various patterns
function makeSelector(enabled: boolean): () => string {
    return (): string => {
        if (enabled === true) {
            return "enabled";
        }
        return "disabled";
    };
}

// Test boolean capture used in numeric context
function makeBoolToNum(val: boolean): () => number {
    return (): number => {
        if (val === true) {
            return 1;
        }
        return 0;
    };
}

console.log(makeSelector(true)());
console.log(makeSelector(false)());
console.log(makeBoolToNum(true)());
console.log(makeBoolToNum(false)());
