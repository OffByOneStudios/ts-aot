// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// CHECK: icmp slt
// CHECK: br i1
// CHECK: add i64
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
