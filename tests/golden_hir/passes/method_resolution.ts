// Test: Array method calls are resolved to builtin operations
// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe

// Array.push is resolved to array.push builtin
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: new_array.boxed
// HIR-CHECK: array.push
// HIR-CHECK: ret

// OUTPUT: 3

function user_main(): number {
  const arr: number[] = [];
  arr.push(1);
  arr.push(2);
  arr.push(3);
  console.log(arr.length);
  return 0;
}
