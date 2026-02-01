// Test: Array.find and Array.findIndex methods
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// find uses runtime call
// HIR-CHECK: call "ts_array_find"
// findIndex uses runtime call
// HIR-CHECK: call "ts_array_findIndex"
// HIR-CHECK: ret

// OUTPUT: 4
// OUTPUT: 3

function user_main(): number {
  const numbers = [1, 2, 3, 4, 5];

  // Find first element > 3
  const found = numbers.find((x: number) => x > 3);
  console.log(found);

  // Find index of first element > 3
  const index = numbers.findIndex((x: number) => x > 3);
  console.log(index);

  return 0;
}
