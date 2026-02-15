// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @__ts_nursery_alloc
// CHECK: store i32 1179402580
// OUTPUT: 42

function user_main(): void {
  const obj = { value: 42 };
  console.log(obj.value);
}
