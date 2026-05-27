// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// GC-001: escaping object literals tenure to old-gen (was @__ts_nursery_alloc)
// CHECK: @ts_gc_alloc_old_gen
// CHECK: store i32 1179402580
// OUTPUT: 42

function user_main(): void {
  const obj = { value: 42 };
  console.log(obj.value);
}
