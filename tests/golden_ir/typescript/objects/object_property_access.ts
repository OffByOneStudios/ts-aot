// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_map_create
// CHECK: @ts_object_set_dynamic
// CHECK: @ts_object_get_dynamic
// OUTPUT: 42

function user_main(): void {
  const obj = { value: 42 };
  console.log(obj.value);
}
