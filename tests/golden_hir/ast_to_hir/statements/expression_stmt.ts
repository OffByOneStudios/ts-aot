// Test: Expression statements generate correct HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: alloca f64
// HIR-CHECK: load f64
// HIR-CHECK: add.f64
// HIR-CHECK: store
// HIR-CHECK: ret

// OUTPUT: 15

function user_main(): number {
  let x: number = 10;
  x + 5;  // Expression statement (value discarded)
  x = x + 5;  // Assignment expression
  console.log(x);
  return 0;
}
