// Test: New expressions for builtin types
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// Array constructor creates array via new_array.boxed
// HIR-CHECK: new_array.boxed
// HIR-CHECK: ret

// OUTPUT: 0

function user_main(): number {
  const arr = new Array<number>();
  console.log(arr.length);

  return 0;
}
