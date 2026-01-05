// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_array_create_specialized
// CHECK: call {{.*}} @ts_value_make_function
// CHECK: call {{.*}} @ts_array_reduce
// OUTPUT: 15

function user_main(): void {
  const arr = [1, 2, 3, 4, 5];
  const sum = arr.reduce((acc, x) => acc + x, 0);
  console.log(sum);
}
