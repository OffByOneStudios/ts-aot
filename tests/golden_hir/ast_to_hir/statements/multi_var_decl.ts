// Test: Multiple variable declarations
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.f64 1
// HIR-CHECK: alloca f64
// HIR-CHECK: store
// HIR-CHECK: const.f64 2
// HIR-CHECK: alloca f64
// HIR-CHECK: store
// HIR-CHECK: const.f64 3
// HIR-CHECK: alloca f64
// HIR-CHECK: store
// HIR-CHECK: ret

// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3

function user_main(): number {
  const a: number = 1, b: number = 2, c: number = 3;

  console.log(a);
  console.log(b);
  console.log(c);

  return 0;
}
