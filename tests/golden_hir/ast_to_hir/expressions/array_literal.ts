// Test: Array literals generate new_array and element initialization
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: new_array
// HIR-CHECK: const.f64 1
// HIR-CHECK: const.f64 2
// HIR-CHECK: const.f64 3
// HIR-CHECK: ret

// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3

function user_main(): number {
  const arr: number[] = [1, 2, 3];

  console.log(arr[0]);
  console.log(arr[1]);
  console.log(arr[2]);

  return 0;
}
