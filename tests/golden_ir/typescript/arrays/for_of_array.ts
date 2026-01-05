// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_array_create_specialized
// CHECK: call {{.*}} @ts_array_length
// CHECK: icmp slt
// CHECK: br i1
// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3

function user_main(): void {
  const arr = [1, 2, 3];
  for (const x of arr) {
    console.log(x);
  }
}
