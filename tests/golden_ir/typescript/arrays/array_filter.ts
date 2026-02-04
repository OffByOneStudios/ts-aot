// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_array_create_sized
// CHECK: @ts_array_filter
// OUTPUT: 6,7,8,9,10

function user_main(): void {
  const arr = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
  const filtered = arr.filter(x => x > 5);
  console.log(filtered.join(','));
}
