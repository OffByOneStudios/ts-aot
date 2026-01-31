// Test: Array.some and Array.every predicate methods
// XFAIL: Array.some/every cause exit code 1 in HIR pipeline
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// some uses runtime call
// HIR-CHECK: call "ts_array_some"
// every uses runtime call
// HIR-CHECK: call "ts_array_every"
// HIR-CHECK: ret

// OUTPUT: true
// OUTPUT: false
// OUTPUT: true
// OUTPUT: false

function user_main(): number {
  const numbers = [1, 2, 3, 4, 5];
  const allPositive = [1, 2, 3];
  const mixed = [1, -2, 3];

  // some: at least one element > 3
  console.log(numbers.some((x: number) => x > 3));

  // some: at least one element > 10
  console.log(numbers.some((x: number) => x > 10));

  // every: all elements > 0
  console.log(allPositive.every((x: number) => x > 0));

  // every: all elements > 0 (should fail due to -2)
  console.log(mixed.every((x: number) => x > 0));

  return 0;
}
