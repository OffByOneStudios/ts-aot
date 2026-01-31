// Test: Constant folding for arithmetic expressions
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// After constant folding, 2 + 3 should become 5
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.f64 5
// HIR-CHECK-NOT: add.f64

// 10 * 4 should become 40
// HIR-CHECK: const.f64 40
// HIR-CHECK-NOT: mul.f64 {{.*}}10{{.*}}4

// HIR-CHECK: ret

// OUTPUT: 5
// OUTPUT: 40
// OUTPUT: 2

function user_main(): number {
  console.log(2 + 3);    // Folded to 5
  console.log(10 * 4);   // Folded to 40
  console.log(10 / 5);   // Folded to 2
  return 0;
}
