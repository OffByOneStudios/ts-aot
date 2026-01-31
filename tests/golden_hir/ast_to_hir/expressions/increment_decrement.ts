// Test: Increment/decrement operators generate correct HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: alloca f64
// HIR-CHECK: load f64
// HIR-CHECK: const.f64 1
// HIR-CHECK: add.f64
// HIR-CHECK: store
// HIR-CHECK: sub.f64
// HIR-CHECK: ret

// OUTPUT: 10
// OUTPUT: 11
// OUTPUT: 12
// OUTPUT: 11

function user_main(): number {
  let x: number = 10;

  console.log(x);    // 10
  x++;
  console.log(x);    // 11
  ++x;
  console.log(x);    // 12
  x--;
  console.log(x);    // 11

  return 0;
}
