// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_value_make_function
// CHECK: call {{.*}} @__ts_map_set_at
// CHECK-NOT: store ptr @double
// OUTPUT: 14.000000

function user_main(): void {
  function double(x: number): number {
    return x * 2;
  }
  const obj = { double };
  console.log(obj.double(7));
}
