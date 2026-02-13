// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_gc_alloc
// CHECK: store i32 1179402580
// CHECK: @ts_object_get_dynamic
// OUTPUT: 42

function user_main(): void {
  const obj = { value: 42 };
  console.log(obj.value);
}
