// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @__ts_nursery_alloc
// CHECK: store i32 1179402580
// OUTPUT: 1
// OUTPUT: 2

function user_main(): void {
  const obj = { a: 1, b: 2 };
  console.log(obj.a);
  console.log(obj.b);
}
