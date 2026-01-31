// Test: Constant folding for comparison expressions
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// After constant folding, comparisons should be resolved
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.bool true
// HIR-CHECK: const.bool false
// HIR-CHECK-NOT: cmp.lt.f64 {{.*}}5{{.*}}10
// HIR-CHECK: ret

// OUTPUT: true
// OUTPUT: false
// OUTPUT: true

function user_main(): number {
  console.log(5 < 10);   // Folded to true
  console.log(10 < 5);   // Folded to false
  console.log(5 === 5);  // Folded to true
  return 0;
}
