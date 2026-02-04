// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: fcmp olt
// CHECK: br i1
// CHECK: fadd
// OUTPUT: 0
// OUTPUT: 1
// OUTPUT: 2

function user_main(): void {
  let i = 0;
  while (i < 3) {
    console.log(i);
    i++;
  }
}
