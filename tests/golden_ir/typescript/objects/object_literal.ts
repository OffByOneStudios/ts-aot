// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_map_create
// CHECK: @ts_object_set_dynamic
// CHECK: @ts_object_set_dynamic
// CHECK: @ts_console_log_value
// CHECK: @ts_console_log_value
// OUTPUT: 1
// OUTPUT: 2

function user_main(): void {
  const obj = { a: 1, b: 2 };
  console.log(obj.a);
  console.log(obj.b);
}
