// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_map_create
// CHECK: call {{.*}} @__ts_map_set_at
// CHECK: call {{.*}} @__ts_map_find_bucket
// CHECK: call {{.*}} @__ts_map_get_value_at
// CHECK-NOT: ts_value_get_string
// OUTPUT: 42

function user_main(): void {
  const obj = { value: 42 };
  console.log(obj.value);
}
