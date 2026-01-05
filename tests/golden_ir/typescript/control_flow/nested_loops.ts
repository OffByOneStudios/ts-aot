// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: br label
// OUTPUT: 0,0
// OUTPUT: 0,1
// OUTPUT: 1,0
// OUTPUT: 1,1

for (let i = 0; i < 2; i++) {
    for (let j = 0; j < 2; j++) {
        console.log(i + ',' + j);
    }
}
