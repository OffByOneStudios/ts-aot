// Test: New expressions for builtin types
// XFAIL: new Array().length returns undefined in HIR pipeline
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: ret

// OUTPUT: 0

function user_main(): number {
  const arr = new Array<number>();
  console.log(arr.length);

  return 0;
}
