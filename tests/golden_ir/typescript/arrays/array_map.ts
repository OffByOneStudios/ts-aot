// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_array_create_sized
// CHECK: @ts_array_map
// NOTE: Currently has a bug - lambda returns boxed values but array stores them as raw pointers
// OUTPUT: 3

function user_main(): void {
  const arr = [1, 2, 3];
  const mapped = arr.map(x => x * 2);
  console.log(mapped.length);
}
