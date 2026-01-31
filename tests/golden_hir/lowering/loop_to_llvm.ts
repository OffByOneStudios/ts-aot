// Test: Loops lower to LLVM basic blocks
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR loops should generate proper basic block structure
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: alloca
// HIR-CHECK: add.f64
// HIR-CHECK: ret

// OUTPUT: 55

function user_main(): number {
  let sum: number = 0;
  for (let i: number = 1; i <= 10; i++) {
    sum += i;
  }
  console.log(sum);  // 1+2+...+10 = 55
  return 0;
}
