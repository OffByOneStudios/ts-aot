// Test: Constant folding optimizes compile-time arithmetic
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Constant folding works: 2 + 3 is folded to 5
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.f64 5
// HIR-CHECK: ret

// OUTPUT: 5

function user_main(): number {
  const x = 2 + 3;  // Folded to 5 at compile time
  console.log(x);
  return 0;
}
