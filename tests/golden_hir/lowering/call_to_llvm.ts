// Test: Function calls lower to LLVM call instructions
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Function is inlined into user_main after optimization
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: add.f64
// HIR-CHECK: ret

// HIR calls should generate LLVM call (monomorphized as add_dbl_dbl)
// HIR-CHECK: define @add_dbl_dbl
// HIR-CHECK: add.f64
// HIR-CHECK: ret

// OUTPUT: 15

function add(a: number, b: number): number {
  return a + b;
}

function user_main(): number {
  const result = add(10, 5);
  console.log(result);
  return 0;
}
