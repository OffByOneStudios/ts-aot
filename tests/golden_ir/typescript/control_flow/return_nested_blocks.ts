// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: br i1
// CHECK: ret
// OUTPUT: 42

function test(x: number): number {
    if (x > 10) {
        if (x > 20) {
            return 42;
        }
        return 20;
    }
    return 10;
}

console.log(test(25));
