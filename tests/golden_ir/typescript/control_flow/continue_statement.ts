// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: br i1
// CHECK: br label
// OUTPUT: 1
// OUTPUT: 3
// OUTPUT: 5

for (let i = 1; i <= 5; i++) {
    if (i % 2 === 0) continue;
    console.log(i);
}
