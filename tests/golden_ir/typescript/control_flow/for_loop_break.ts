// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: br i1
// CHECK: br label
// OUTPUT: 0
// OUTPUT: 1
// OUTPUT: 2

for (let i = 0; i < 10; i++) {
    if (i === 3) break;
    console.log(i);
}
