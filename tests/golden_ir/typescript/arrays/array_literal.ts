// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_array_create_sized
// CHECK: @ts_console_log_int
// OUTPUT: 3

function user_main(): void {
  const arr = [1, 2, 3];
  console.log(arr.length);
}
