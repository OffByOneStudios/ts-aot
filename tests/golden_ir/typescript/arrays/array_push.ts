// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_array_create
// CHECK: @ts_array_push
// CHECK: @ts_console_log_int
// OUTPUT: 4

function user_main(): void {
  const arr = [1, 2, 3];
  arr.push(42);
  console.log(arr.length);
}
