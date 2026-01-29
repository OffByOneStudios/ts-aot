// Test: Unary operators in HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.bool true
// HIR-CHECK: not.bool
// HIR-CHECK: ret

// OUTPUT: false

function user_main(): number {
  const x: boolean = true;
  const notX: boolean = !x;
  console.log(notX);
  return 0;
}
