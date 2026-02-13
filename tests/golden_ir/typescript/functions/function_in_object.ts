// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_gc_alloc
// OUTPUT: 14

function user_main(): void {
  function double(x: number): number {
    return x * 2;
  }
  const obj = { double };
  console.log(obj.double(7));
}
