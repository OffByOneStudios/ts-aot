// Test: Comparison operations lower to LLVM fcmp
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR comparisons should lower to LLVM fcmp instructions
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: cmp.lt.f64
// HIR-CHECK: cmp.gt.f64
// HIR-CHECK: cmp.eq.f64
// HIR-CHECK: ret

// OUTPUT: true
// OUTPUT: true
// OUTPUT: true
// OUTPUT: false

function user_main(): number {
  const a: number = 5;
  const b: number = 10;

  console.log(a < b);   // true
  console.log(b > a);   // true
  console.log(a === 5); // true
  console.log(a !== 5); // false

  return 0;
}
