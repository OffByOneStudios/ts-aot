// Test: Allocas lower to LLVM alloca
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR alloca should generate LLVM alloca
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: alloca f64
// HIR-CHECK: store
// HIR-CHECK: load f64
// HIR-CHECK: ret

// OUTPUT: 42

function user_main(): number {
  let x: number = 42;
  console.log(x);
  return 0;
}
