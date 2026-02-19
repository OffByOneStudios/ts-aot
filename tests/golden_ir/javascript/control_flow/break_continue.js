// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 1
// OUTPUT: 3
// OUTPUT: 5

for (var i = 0; i < 10; i++) {
    if (i % 2 === 0) continue;
    if (i > 5) break;
    console.log(i);
}
