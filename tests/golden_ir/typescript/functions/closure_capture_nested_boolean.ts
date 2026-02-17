// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: inner: yes
// OUTPUT: inner: no

// Test: Boolean capture through two levels of nested closures
function outer(flag: boolean): () => () => string {
    return (): () => string => {
        return (): string => {
            if (flag === true) {
                return "yes";
            }
            return "no";
        };
    };
}

console.log("inner: " + outer(true)()());
console.log("inner: " + outer(false)()());
