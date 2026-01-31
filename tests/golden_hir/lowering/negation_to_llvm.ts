// Test: Negation operations lower correctly
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR negation should lower to LLVM fneg
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: neg.f64
// HIR-CHECK: not.bool
// HIR-CHECK: ret

// OUTPUT: -10
// OUTPUT: false

function user_main(): number {
  const x: number = 10;
  const b: boolean = true;

  console.log(-x);   // -10
  console.log(!b);   // false

  return 0;
}
