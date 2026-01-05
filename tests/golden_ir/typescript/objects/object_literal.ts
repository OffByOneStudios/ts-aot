// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_map_create
// CHECK: call {{.*}} @__ts_map_set_at
// CHECK: call {{.*}} @__ts_map_set_at
// CHECK: call {{.*}} @ts_console_log_int
// CHECK: call {{.*}} @ts_console_log_int
// OUTPUT: 1
// OUTPUT: 2

function user_main(): void {
  const obj = { a: 1, b: 2 };
  console.log(obj.a);
  console.log(obj.b);
}
