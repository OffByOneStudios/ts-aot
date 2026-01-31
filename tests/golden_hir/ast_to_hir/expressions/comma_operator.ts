// Test: Comma operator evaluates expressions left to right
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// After DCE, intermediate values are eliminated
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.f64 3
// HIR-CHECK: ret

// OUTPUT: 3

function user_main(): number {
  const result = (1, 2, 3);
  console.log(result);
  return 0;
}
