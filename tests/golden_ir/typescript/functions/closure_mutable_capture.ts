// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// CHECK: @ts_closure_get_func
// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3

function counter() {
    let count = 0;
    return () => {
        count = count + 1;
        return count;
    };
}

const c = counter();
console.log(c());
console.log(c());
console.log(c());
